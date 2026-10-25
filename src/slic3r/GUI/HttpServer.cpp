#include "HttpServer.hpp"
#include "DeviceImageProxy.hpp"
#include "PrinterCoverRoute.hpp"
#include <boost/log/trivial.hpp>
#include "GUI_App.hpp"
#include "libslic3r/Thread.hpp"
#include "slic3r/Utils/Http.hpp"
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <random>
#if defined(__linux__) || defined(__LINUX__)
#include "video/WebRTCDecoder.h"
#include <boost/asio/steady_timer.hpp>
#endif

#include "buildinfo.h"
#if ENABLE_FFMPEG
#include "video/RTSPDecoder.h"
#endif
namespace Slic3r {
namespace GUI {
namespace pt = boost::property_tree;

int http_headers::content_length()
{
    auto request = headers.find("content-length");
    if (request != headers.end()) {
        std::stringstream ssLength(request->second);
        int               content_length;
        ssLength >> content_length;
        return content_length;
    }
    return 0;
}

std::string http_headers::get(const std::string& name) const
{
    for (const auto& [key, value] : headers) {
        if (boost::algorithm::iequals(boost::algorithm::trim_copy(key), name))
            return boost::algorithm::trim_copy(value);
    }
    return {};
}

void http_headers::on_read_header(std::string line)
{
    std::stringstream ssHeader(line);
    std::string       headerName;
    std::getline(ssHeader, headerName, ':');

    std::string value;
    std::getline(ssHeader, value);
    headers[headerName] = value;
}

void http_headers::on_read_request_line(std::string line)
{
    std::stringstream ssRequestLine(line);
    ssRequestLine >> method;
    ssRequestLine >> url;
    ssRequestLine >> version;
}

std::string url_get_param(const std::string& url, const std::string& key)
{
    size_t start = url.find(key);
    if (start == std::string::npos) return "";
    size_t eq = url.find('=', start);
    if (eq == std::string::npos) return "";
    std::string key_str = url.substr(start, eq - start);
    if (key_str != key)
        return "";
    start += key.size() + 1;
    size_t end = url.find('&', start);
    if (end == std::string::npos) end = url.length(); // Last param
    std::string result = url.substr(start, end - start);
    return result;
}

std::string url_get_param_ignore(const std::string& url, const std::string& key)
{
    size_t start = url.find(key);
    if (start == std::string::npos) return "";
    size_t eq = url.find('=', start);
    if (eq == std::string::npos) return "";
    std::string key_str = url.substr(start, eq - start);
    if (key_str != key)
        return "";
    start += key.size() + 1;
    size_t end = url.length(); // Last param
    std::string result = url.substr(start, end - start);
    return result;
}

static void trim_http_line_end(std::string& line)
{
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
}

std::string build_http_response(int status_code, const std::string& content_type, const std::string& content);

namespace {

// Private (RFC 1918) or link-local IPv4 address in dotted form, nothing else (no names,
// ports, user info or spaces). Used for the proxy and video stream targets.
bool is_private_lan_ipv4(const std::string& address)
{
    if (address.empty() || address.find_first_not_of("0123456789.") != std::string::npos)
        return false;
    boost::system::error_code ec;
    const auto parsed_address = boost::asio::ip::make_address(address, ec);
    if (ec || !parsed_address.is_v4())
        return false;
    const auto b = parsed_address.to_v4().to_bytes();
    return b[0] == 10 || (b[0] == 172 && (b[1] & 0xF0) == 16) || (b[0] == 192 && b[1] == 168) ||
           (b[0] == 169 && b[1] == 254 && b[2] != 0 && b[2] != 255);
}

// DNS rebinding guard: a page of another site that resolves its own name to 127.0.0.1 still
// sends its own name in Host. Only "localhost:<port>" and "127.0.0.1:<port>" are accepted.
bool is_local_host_header(const std::string& host, unsigned port)
{
    const std::string suffix = ":" + std::to_string(port);
    return boost::algorithm::iequals(host, "localhost" + suffix) || host == "127.0.0.1" + suffix;
}

bool constant_time_equals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size() || a.empty())
        return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

std::string random_hex_token(size_t digits)
{
    std::random_device rd;
    static const char hex[] = "0123456789abcdef";
    std::string token;
    token.reserve(digits);
    while (token.size() < digits) {
        unsigned int bits = rd();
        for (int i = 0; i < 8 && token.size() < digits; ++i, bits >>= 4)
            token += hex[bits & 15];
    }
    return token;
}

} // namespace

void session::start()
{
    read_first_line();
}

void session::stop()
{
    boost::system::error_code ignored_ec;
    socket.shutdown(boost::asio::socket_base::shutdown_both, ignored_ec);
    socket.close(ignored_ec);
}

void session::read_first_line()
{
    auto self(shared_from_this());

    async_read_until(socket, buff, "\r\n", [this, self](const boost::beast::error_code& e, std::size_t s) {
        if (!e) {
            std::string  line;
            std::istream stream{&buff};
            std::getline(stream, line);
            trim_http_line_end(line);
            headers.on_read_request_line(line);
            read_next_line();
        } else if (e != boost::asio::error::operation_aborted) {
            server.stop(self);
        }
    });
}

void session::read_body()
{
    auto self(shared_from_this());

    int                                nbuffer = 1000;
    std::shared_ptr<std::vector<char>> bufptr  = std::make_shared<std::vector<char>>(nbuffer);
    async_read(socket, boost::asio::buffer(*bufptr, nbuffer),
               [this, self](const boost::beast::error_code& e, std::size_t s) { server.stop(self); });
}

void session::read_next_line()
{
    auto self(shared_from_this());

    async_read_until(socket, buff, "\r\n", [this, self](const boost::beast::error_code& e, std::size_t s) {
        if (!e) {
            std::string  line;
            std::istream stream{&buff};
            std::getline(stream, line);
            trim_http_line_end(line);
            headers.on_read_header(line);

            if (line.length() == 0) {
                if (!is_local_host_header(headers.get("host"), server.server.port)) {
                    BOOST_LOG_TRIVIAL(warning) << "creality server: request with a foreign Host header rejected";
                    write_response(build_http_response(403, "text/plain", "Forbidden"));
                    return;
                }
                if (headers.content_length() == 0) {
                    if (headers.method == "OPTIONS") {
                        // Ignore http OPTIONS
                        server.stop(self);
                        return;
                    }

                    const std::string url_str = Http::url_decode(headers.get_url());
                    if (url_str.find("/device_image") == 0) {
                        this->handle_device_image_request(url_str);
                        return;
                    }

                    if (url_str.find("/proxy") == 0){
                        // 处理代理请求
                        this->handle_proxy_request(url_str);
                        return ;
                    }
                    else if (url_str.find("/videostream") == 0 || url_str.find("/rtspvideostream") == 0) {
                        
                        std::string   ip           = url_get_param(url_str, "ip");
                        if (!is_private_lan_ipv4(ip)) {
                            write_response(build_http_response(400, "text/plain", "Invalid video stream request"));
                            return;
                        }
                        bool isRtsp = url_str.find("/rtspvideostream") == 0;
                        std::string video_url = "";
                        HttpServer *pServer = wxGetApp().get_server();
                        //pServer->mjpeg_server_started = true;

                        if(isRtsp)
                        {
                            video_url = (boost::format("rtsp://%1%/ch0_0") % ip).str();
                            #if ENABLE_FFMPEG
                            RTSPDecoder::GetInstance()->startPlay(video_url);
                            #endif
                        }
                        else
                        {
                            video_url = (boost::format("http://%1%:8000/call/webrtc_local") % ip).str();
                              #if defined(__linux__) || defined(__LINUX__)
                                WebRTCDecoder::GetInstance()->startPlay(video_url); 
                              #endif
                        }
                       
                      
                        const std::string header =
                                    "HTTP/1.1 200 OK\r\n"
                                    "Content-Type: multipart/x-mixed-replace; boundary=boundarydonotcross\r\n"
                                    "Cache-Control: no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0\r\n"
                                    "pragma:no-cache\r\n"
                                    "Connection: close\r\n\r\n";
                        std::shared_ptr<std::string> frame_header_ptr = std::make_shared<std::string>(header);
                        async_write(socket,  boost::asio::buffer(frame_header_ptr->data(), frame_header_ptr->size()),[this, self,pServer,isRtsp,frame_header_ptr](const boost::beast::error_code& e, std::size_t s) {
                            if(!e)
                            {
                                if(!this->m_video_timer)
                                {
                                    this->m_video_timer = new boost::asio::deadline_timer(server.io_service, boost::posix_time::milliseconds(100));
                                }
                                this->sendFrame(e,socket,isRtsp);
                            }else{
                                //pServer->mjpeg_server_started = false;
                            }
                            
                        });
                        
                    }else{
                        const auto        resp    = server.server.m_request_handler(url_str);
                        std::stringstream ssOut;
                        resp->write_response(ssOut);
                        std::shared_ptr<std::string> str = std::make_shared<std::string>(ssOut.str());
                        async_write(socket, boost::asio::buffer(str->c_str(), str->length()),
                                    [this, self, str](const boost::beast::error_code& e, std::size_t s) {
                            server.stop(self);
                        });
                    }
                } else {
                    read_body();
                }
            } else {
                read_next_line();
            }
        } else if (e != boost::asio::error::operation_aborted) {
            server.stop(self);
        }
    });
}

void session::sendFrame(const boost::system::error_code &ec,boost::asio::ip::tcp::socket &socket, bool is_rtsp)
{
        std::vector<unsigned char> copy_frame;
    
        if(is_rtsp)
        {
            #if ENABLE_FFMPEG
            RTSPDecoder::GetInstance()->getFrameData(copy_frame);
            #endif
        }else{
            #if defined(__linux__) || defined(__LINUX__)
            copy_frame = WebRTCDecoder::GetInstance()->getFrameData();
            #endif
        }
        
        //this->frame_mutex_.unlock();
        if(copy_frame.size()==0)
        {
            this->m_video_timer->cancel();
            this->m_video_timer->expires_at(this->m_video_timer->expires_at()+boost::posix_time::milliseconds(150));
            this->m_video_timer->async_wait([this, ec, &socket ,is_rtsp](const auto& error) {
                    if (!error) {
                        this->sendFrame(ec,socket,is_rtsp);
                    }
                });
            return;
        }
       
        
        const std::string frame_header =
                    "--boundarydonotcross\r\n"
                    "Content-Type: image/jpeg\r\n"
                    "Content-Length: " + 
                    std::to_string(copy_frame.size()) + "\r\n\r\n";
        
        std::shared_ptr<std::string> frame_header_ptr = std::make_shared<std::string>(frame_header);
        std::shared_ptr<std::vector<unsigned char>> frame_ptr = std::make_shared<std::vector<unsigned char>>(copy_frame);
        std::vector<boost::asio::const_buffer> buffers;
        buffers.emplace_back(boost::asio::buffer(frame_header_ptr->data(), frame_header_ptr->size()));
        buffers.emplace_back(boost::asio::buffer(frame_ptr->data(), frame_ptr->size()));
        
        boost::asio::async_write(socket,  buffers,[this,&socket,is_rtsp,frame_header_ptr,frame_ptr](const boost::beast::error_code& ec, std::size_t s) {
            
            if(!ec)
            {
                //std::this_thread::sleep_for(std::chrono::milliseconds(80));
                //this->sendFrame(ec,socket);
                this->m_video_timer->cancel();
                this->m_video_timer->expires_at(this->m_video_timer->expires_at()+boost::posix_time::milliseconds(150));
                this->m_video_timer->async_wait([this, ec, &socket, is_rtsp](const auto& error) {
                    if (!error) {
                        this->sendFrame(ec,socket,is_rtsp);
                    }
                });
            }else{
                //this->mjpeg_server_started = false;
                BOOST_LOG_TRIVIAL(error) << "send frame error: " << ec.message();
            }
            
        });
}

HttpServer::IOServer::IOServer(HttpServer& server)
    : server(server)
    , acceptor(io_service)
    , device_image_proxy(std::make_shared<DeviceImageProxy>(io_service))
{
    const boost::asio::ip::tcp::endpoint endpoint(
        boost::asio::ip::address_v4::loopback(), server.port);

    // Do not use the endpoint constructor: it sets SO_REUSEADDR, which on
    // Windows lets another process silently share this listening port.
    acceptor.open(endpoint.protocol());

#ifdef _WIN32
    // Windows: prohibit another process from sharing this local proxy port.
    BOOL exclusive_address_use = TRUE;
    if (::setsockopt(acceptor.native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                     reinterpret_cast<const char*>(&exclusive_address_use),
                     sizeof(exclusive_address_use)) == SOCKET_ERROR) {
        throw boost::system::system_error(
            boost::system::error_code(::WSAGetLastError(),
                                      boost::system::system_category()),
            "Failed to enable exclusive HTTP server port binding");
    }
#else
    // Preserve quick restart behavior on POSIX.
    acceptor.set_option(boost::asio::socket_base::reuse_address(true));
#endif

    acceptor.bind(endpoint);
}

void HttpServer::IOServer::do_accept()
{
    acceptor.async_accept([this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
        if (!acceptor.is_open()) {
            return;
        }

        if (!ec) {
            const auto ss = std::make_shared<session>(*this, std::move(socket));
            start(ss);
        }

        do_accept();
    });
}

void HttpServer::IOServer::start(std::shared_ptr<session> session)
{
    sessions.insert(session);
    session->start();
}

void HttpServer::IOServer::stop(std::shared_ptr<session> session)
{
    sessions.erase(session);
    session->stop();
}

void HttpServer::IOServer::stop_all()
{
    for (auto s : sessions) {
        s->stop();
    }
    sessions.clear();
}


HttpServer::HttpServer(boost::asio::ip::port_type port) : port(port), m_proxy_token(random_hex_token(32)) {}

std::string HttpServer::proxy_token_user_script() const
{
    // Inserts "pa_t=<token>&" right after "/proxy?" (the server reads it from there and does not
    // forward it), only for the local server and only on the app's own pages (file:// or the
    // local server), so the token never reaches another site's frames.
    return std::string(
               "(function(){if(window.__msProxyPatched)return;window.__msProxyPatched=true;"
               "var l=location;if(l.protocol!=='file:'&&!/^(localhost|127\\.0\\.0\\.1)$/i.test(l.hostname))return;"
               "var t='") + m_proxy_token + "';"
               "var re=/^((?:https?:\\/\\/(?:localhost|127\\.0\\.0\\.1):\\d+)?\\/proxy\\?)(?!pa_t=)/i;"
               "function fix(u){if(typeof u!=='string'&&!(u instanceof URL))return u;var s=String(u),m=re.exec(s);"
               "return m?m[1]+'pa_t='+t+'&'+s.slice(m[1].length):u;}"
               "var o=XMLHttpRequest.prototype.open;"
               "XMLHttpRequest.prototype.open=function(){if(arguments.length>1)arguments[1]=fix(arguments[1]);return o.apply(this,arguments);};"
               "var f=window.fetch;if(f)window.fetch=function(i,n){return f.call(this,fix(i),n);};"
               "})();";
}

namespace {

bool is_port_unavailable(const boost::system::error_code& ec)
{
    if (ec == boost::asio::error::address_in_use)
        return true;

#ifdef _WIN32
    // WSAEACCES (10013): reserved, occupied, or access-restricted candidate.
    return ec == boost::asio::error::access_denied;
#else
    return false;
#endif
}

} // namespace

void HttpServer::start()
{
    if (start_http_server)
        return;

    const auto initial_port = port;
    constexpr int max_port_attempts = 20;
    for (int attempt = 0; attempt < max_port_attempts; ++attempt) {
        port = static_cast<boost::asio::ip::port_type>(initial_port + attempt);
        try {
            server_ = std::make_unique<IOServer>(*this);
            server_->acceptor.listen();
            break;
        } catch (const boost::system::system_error& e) {
            server_.reset();
            if (!is_port_unavailable(e.code())) {
                BOOST_LOG_TRIVIAL(error) << "Failed to start HTTP server on port " << port << ": " << e.what();
                port = initial_port;
                return;
            }
        }
    }

    if (!server_) {
        BOOST_LOG_TRIVIAL(error) << "Failed to find a free HTTP server port starting at " << initial_port;
        port = initial_port;
        return;
    }

    BOOST_LOG_TRIVIAL(info) << "start_http_service...:" << port;
    start_http_server = true;
    m_http_server_thread = create_thread([this] {
        set_current_thread_name("http_server");
        try {
            server_->do_accept();
            server_->io_service.run();
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "HTTP server stopped unexpectedly: " << e.what();
            start_http_server = false;
        }
    });
}

void HttpServer::stop()
{
    if(!start_http_server)
        return;
    start_http_server = false;
    if (server_) {
        server_->acceptor.close();
        server_->stop_all();
        server_->io_service.stop();
    }
    if (m_http_server_thread.joinable())
        m_http_server_thread.join();
    server_.reset();
#if ENABLE_FFMPEG
    RTSPDecoder::GetInstance()->stopPlay();
#endif
}

void HttpServer::set_request_handler(const std::function<std::shared_ptr<Response>(const std::string&)>& request_handler)
{
    this->m_request_handler = request_handler;
}
// 根据文件扩展名确定Content-Type
std::string get_content_type(const std::string& file_extension) {
    static std::map<std::string, std::string> content_types = {
        {".html", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".jpg", "image/jpeg"},
        {".png", "image/png"},  
        {".svg", "image/svg+xml"},
        {".ttf", "font/ttf"}
        // 可以继续添加更多的文件扩展名和对应的Content-Type映射
    };
    auto it = content_types.find(file_extension);
    if (it!= content_types.end()) {
        return it->second;
    }
    return "application/octet-stream";
}

// 构建HTTP响应消息
std::string build_http_response(int status_code, const std::string& content_type, const std::string& content) {
    std::stringstream response;
    switch (status_code) {
    case 200:
        response << "HTTP/1.1 200 OK\r\n";
        break;
    case 400:
        response << "HTTP/1.1 400 Bad Request\r\n";
        break;
    case 403:
        response << "HTTP/1.1 403 Forbidden\r\n";
        break;
    case 404:
        response << "HTTP/1.1 404 Not Found\r\n";
        break;
    case 405:
        response << "HTTP/1.1 405 Method Not Allowed\r\n";
        break;
    case 500:
        response << "HTTP/1.1 500 Internal Server Error\r\n";
        break;
    case 502:
        response << "HTTP/1.1 502 Bad Gateway\r\n";
        break;
    case 503:
        response << "HTTP/1.1 503 Service Unavailable\r\n";
        break;
    case 504:
        response << "HTTP/1.1 504 Gateway Timeout\r\n";
        break;
        // 可以添加更多的状态码处理逻辑
    }
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << content.length() << "\r\n";
    response << "Connection: close\r\n";
    response << "Cache-Control: no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0\r\n";
    response << "pragma:no-cache\r\n";
    response << "\r\n";
    response << content;
    return response.str();
}
// 解析URL，提取路径和参数
void parse_url(const std::string& url, std::string& path, std::string& params) {
    // 简单的正则表达式模式，用于匹配URL中的路径和查询参数部分
    // 这里的正则表达式是一种简化示意，实际应用中可根据更严格的URL规范进一步完善
    boost::regex pattern("(/[^?]*)?(\\?.*)?");
    boost::smatch matches;

    if (boost::regex_match(url, matches, pattern)) {
        if (matches.size() >= 1) {
            path = matches[1];
            if (matches.size() >= 2) {
                params = matches[2];
            } else {
                params = "";
            }
        }
    }
}
void session::do_write_proxy(SocketPtr proxysocket, const std::string& target_host,const std::string& target_path)
{
    // 构造 HTTP 请求
        std::string request = "GET " + target_path + " HTTP/1.1\r\n";
        request += "Host: " + target_host + "\r\n";
        request += "Connection: close\r\n\r\n";

        std::shared_ptr<std::string> str = std::make_shared<std::string>(request);
        // 发送请求
        //boost::asio::write(socket, boost::asio::buffer(request));
        proxysocket->async_send(boost::asio::buffer(str->c_str(), str->length()), [str](const boost::system::error_code& ec, std::size_t) {
            if (ec)
                BOOST_LOG_TRIVIAL(debug) << "Proxy request send failed: " << ec.message();
        });
        timer_.expires_after(std::chrono::seconds(5));
}
void session::write_response(const std::string& response)
{
    auto self(shared_from_this());
    std::shared_ptr<std::string> str = std::make_shared<std::string>(response);
                        async_write(socket, boost::asio::buffer(str->c_str(), str->length()),
                                    [this, self, str](const boost::beast::error_code& e, std::size_t s) {
                            if (e)
                                BOOST_LOG_TRIVIAL(debug) << "Error writing response: " << e.message();
                            server.stop(self);
                        });
}
void session::do_read(SocketPtr socket_ptr)
{
    auto self(shared_from_this());
    async_read_until(*socket_ptr, proxy_buff, '\r', [this, self,socket_ptr](const boost::beast::error_code& e, std::size_t s) {
        timer_.cancel();
        if (!e) {
            std::string  line, ignore;
            std::istream stream{&proxy_buff};
            std::getline(stream, line, '\r');
            //{}
            std::getline(stream, ignore, '\n');
            timer_.expires_after(std::chrono::seconds(5));
            async_read(*socket_ptr, proxy_buff,[this](const boost::system::error_code& ec, size_t) {
                //if(ec) {
                //    return;
                //}
                timer_.cancel();
                std::ostringstream response_body;
                response_body << "HTTP/1.1 200 OK\r\n";
                response_body << "Content-Type: application/json\r\n";
                // The Device page is loaded from file:// (origin "null") and reads the proxy
                // with XHR: allow only that origin; same-origin pages need no CORS header.
                // Any file:// page shares that origin: the session token is the real check.
                if (headers.get("origin") == "null")
                    response_body << "Access-Control-Allow-Origin: null\r\n";

                std::istream response_stream1{&proxy_buff};
                std::string allLine,line;
                bool body_started = false;
                while (std::getline(response_stream1, line))
                {
                    if(line == "\r"&&!body_started)
                    {
                        body_started = true; // 开始读取body
                        continue; // 跳过空行
                    }
                    if(body_started)
                        allLine += line;
                }
                try{
                    nlohmann::json json_data = nlohmann::json::parse(allLine);
                    std::string body = json_data.dump();
                    response_body << "Content-Length: " << body.length() << "\r\n";
                    response_body << "connection: keep-alive\r\n";
                    response_body << "\r\n"; // 结束头部
                    response_body <<  body;
                    write_response(response_body.str());
                    }catch (nlohmann::json::parse_error& e) {
                        BOOST_LOG_TRIVIAL(debug) << "Proxy response JSON parse error: " << e.what();
                        write_response(
                            build_http_response(500, "text/plain", "Internal server error: JSON parse error"));
                        return;
                    }
                });
            // read body
        } else if (e != boost::asio::error::operation_aborted) {
            server.stop(self);
        }
    });
    timer_.expires_after(std::chrono::seconds(5));
                   
}
namespace {

std::string device_image_query_param(const std::string& url, const std::string& key)
{
    const std::size_t query_pos = url.find('?');
    if (query_pos == std::string::npos)
        return {};

    std::size_t start = query_pos + 1;
    while (start < url.size()) {
        std::size_t end = url.find('&', start);
        if (end == std::string::npos)
            end = url.size();

        const std::size_t equal = url.find('=', start);
        if (equal != std::string::npos && equal < end &&
            url.compare(start, equal - start, key) == 0) {
            return url.substr(equal + 1, end - equal - 1);
        }

        if (end == url.size())
            break;
        start = end + 1;
    }

    return {};
}

bool is_valid_device_image_address(const std::string& address)
{
    boost::system::error_code ec;
    const auto parsed_address = boost::asio::ip::make_address(address, ec);
    return !ec && parsed_address.is_v4() &&
           !parsed_address.is_unspecified() &&
           !parsed_address.is_loopback() &&
           !parsed_address.is_multicast();
}

// The pages only use the proxy for the protocal.csp API of the older Creality WiFi printers.
bool is_valid_proxy_path(const std::string& target_path)
{
    static const std::string prefix = "/protocal.csp";
    if (target_path.compare(0, prefix.size(), prefix) != 0)
        return false;
    if (target_path.size() > prefix.size() && target_path[prefix.size()] != '?')
        return false;
    for (unsigned char c : target_path) {
        if (c <= 0x20 || c == 0x7F)
            return false;
    }
    return true;
}

bool is_valid_device_image_target(const std::string& target_path)
{
    if (target_path.find('\r') != std::string::npos ||
        target_path.find('\n') != std::string::npos) {
        return false;
    }

    static const std::string thumbnail_prefix = "/downloads/humbnail/";
    if (target_path.compare(0, thumbnail_prefix.size(), thumbnail_prefix) == 0)
        return target_path.size() > thumbnail_prefix.size();

    return target_path == "/downloads/original/current_print_image.png" ||
           target_path ==
               "/downloads/cloudSliceHumbnail/cloudSliceHumbnail.png";
}

int device_image_error_status(unsigned status_code)
{
    switch (status_code) {
    case 400:
    case 404:
    case 502:
    case 503:
    case 504:
        return static_cast<int>(status_code);
    default:
        return 502;
    }
}

} // namespace

void session::handle_device_image_request(const std::string& url)
{
    const std::string address     = device_image_query_param(url, "address");
    const std::string target_path = device_image_query_param(url, "path");
    const std::string secure      = device_image_query_param(url, "secure");
    const std::string version     = device_image_query_param(url, "v");

    BOOST_LOG_TRIVIAL(debug) << "Device image request: url=" << url
                            << ", address=" << address
                            << ", target_path=" << target_path
                            << ", secure=" << secure;

    if (headers.method != "GET") {
        write_response(build_http_response(405, "text/plain", "Method not allowed"));
        return;
    }

    const bool secure_connection = secure == "1";
    if ((secure != "0" && secure != "1") || !is_valid_device_image_address(address) ||
        !is_valid_device_image_target(target_path)) {
        write_response(build_http_response(400, "text/plain", "Invalid device image request"));
        return;
    }

    if (!server.device_image_proxy) {
        write_response(build_http_response(500, "text/plain", "Device image proxy is unavailable"));
        return;
    }

    const auto self = shared_from_this();
    server.device_image_proxy->async_get(
        DeviceImageRequest{address, target_path, secure_connection},
        [self, cacheable = !version.empty()](DeviceImageResult result) mutable {
            self->write_device_image_response(std::move(result), cacheable);
        });
}

void session::write_device_image_response(DeviceImageResult result, bool cacheable)
{
    if (result.status_code != 200 || result.body.empty()) {
        const int status_code = device_image_error_status(result.status_code);
        BOOST_LOG_TRIVIAL(error) << "Device image request failed: status=" << result.status_code
                                 << ", error=" << result.error;
        write_response(build_http_response(status_code, "text/plain",
                                           result.error.empty() ? "Unable to fetch device image" : result.error));
        return;
    }

    using ImageResponse = boost::beast::http::response<
        boost::beast::http::vector_body<unsigned char>>;

    auto response = std::make_shared<ImageResponse>(boost::beast::http::status::ok, 11);
    response->set(boost::beast::http::field::content_type,
                  result.content_type.empty() ? "application/octet-stream" : result.content_type);
    response->set(boost::beast::http::field::cache_control,
                  cacheable ? "private, max-age=31536000, immutable" : "no-store");
    response->keep_alive(false);
    response->body() = std::move(result.body);
    response->prepare_payload();

    const auto self = shared_from_this();
    boost::beast::http::async_write(
        socket,
        *response,
        [this, self, response](const boost::beast::error_code& ec, std::size_t) {
            if (ec)
                BOOST_LOG_TRIVIAL(error) << "Device image response write failed: " << ec.message();
            server.stop(self);
        });
}

void session::handle_proxy_request(const std::string& request_url)
{
    if (headers.method != "GET") {
        write_response(build_http_response(405, "text/plain", "Method not allowed"));
        return;
    }

    // The session token comes first ("/proxy?pa_t=<token>&host=...", added by the user script
    // of the app's WebViews) and is removed before the parameters are parsed, because the
    // "path" parameter runs to the end of the URL.
    static const std::string token_prefix = "/proxy?pa_t=";
    std::string url;
    bool        authorized = false;
    if (request_url.compare(0, token_prefix.size(), token_prefix) == 0) {
        const size_t amp = request_url.find('&', token_prefix.size());
        if (amp != std::string::npos) {
            authorized = constant_time_equals(request_url.substr(token_prefix.size(), amp - token_prefix.size()),
                                              server.server.proxy_token());
            url = "/proxy?" + request_url.substr(amp + 1);
        }
    }
    if (!authorized) {
        BOOST_LOG_TRIVIAL(warning) << "Proxy request without a valid session token rejected";
        write_response(build_http_response(403, "text/plain", "Forbidden"));
        return;
    }
    BOOST_LOG_TRIVIAL(debug) << "Proxy request: " << url;

    // 解析目标服务器地址和路径
    std::string target_host = url_get_param(url, "host");
    std::string target_path = url_get_param_ignore(url, "path");

    if (!is_private_lan_ipv4(target_host) || !is_valid_proxy_path(target_path)) {
        BOOST_LOG_TRIVIAL(warning) << "Invalid proxy request rejected";
        write_response(build_http_response(400, "text/plain", "Invalid proxy request"));
        return;
    }

    try {
       
        SocketPtr socket_ptr = nullptr;
        auto it = m_proxy_sockets.find(target_host);
            if(it != m_proxy_sockets.end() && it->second->is_open())
            {
                socket_ptr = it->second;
            }else{
                socket_ptr = std::make_shared<tcp::socket>(server.io_service);
                m_proxy_sockets.emplace(target_host,socket_ptr);
            }
        
        socket_ptr->async_connect(
            tcp::endpoint(boost::asio::ip::make_address(target_host), 81),
            [this,socket_ptr,target_host,target_path](const boost::system::error_code& ec) {
                if (!ec) {
                    // 连接成功，可以继续使用 socket_ptr
                    timer_.cancel();
                    
                     this->do_write_proxy(socket_ptr,target_host, target_path);
                     this->do_read(socket_ptr);
                }else{
                    BOOST_LOG_TRIVIAL(debug) << "Proxy: error connecting to target host: " << ec.message();
                    write_response(
                        build_http_response(500, "text/plain", "Internal server error: Unable to connect to target host"));
                }
            });
        timer_.expires_after(std::chrono::seconds(5));
        timer_.async_wait([this,socket_ptr](std::error_code ec) {
            if (!ec) {
                //write_response(
                //        build_http_response(500, "text/plain", "timeout: Unable to connect to target host"));
                
                socket_ptr->close(); // 超时后关闭socket连接
               // 超时取消后的清理工作（如果有）
            } else {
                // 处理其他错误情况（理论上不应该发生）
            }
        });

        // 返回代理响应
        return ;
    } catch (std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Proxy request failed: " << e.what();
        return ;
    }
}

namespace {

// Windows opens a device, not a file, for these names in any folder and with any extension
// ("nul.js", "COM1.txt", "con .html").
bool is_reserved_dos_device_name(const std::string& segment)
{
    std::string base = segment.substr(0, segment.find('.'));
    while (!base.empty() && base.back() == ' ')
        base.pop_back();
    for (char& c : base) {
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    }
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" || base == "CONIN$" || base == "CONOUT$")
        return true;
    if (base.size() > 3 && (base.compare(0, 3, "COM") == 0 || base.compare(0, 3, "LPT") == 0)) {
        const std::string number = base.substr(3);
        // Digits 1-9, and the superscripts 1-3 (UTF-8) that Windows also maps to these ports.
        return (number.size() == 1 && number[0] >= '1' && number[0] <= '9') ||
               number == "\xC2\xB9" || number == "\xC2\xB2" || number == "\xC2\xB3";
    }
    return false;
}

// Maps the URL path `relative` ('/' separated, already URL-decoded) onto a file inside `root`.
// Empty when the path could leave the root: "." or ".." segments, backslashes, drive letters
// or streams (':'), NUL and other control characters, or segments that Windows would rewrite
// (trailing dots or spaces), or reserved DOS device names.
fs::path static_file_path(const fs::path& root, const std::string& relative)
{
    if (relative.empty())
        return {};
    for (unsigned char c : relative) {
        if (c < 0x20 || c == 0x7F || c == '\\' || c == ':')
            return {};
    }
    fs::path target = root;
    std::vector<std::string> segments;
    boost::algorithm::split(segments, relative, boost::algorithm::is_any_of("/"));
    bool any = false;
    for (const std::string& segment : segments) {
        if (segment.empty())
            continue;
        if (segment.back() == '.' || segment.back() == ' ' || is_reserved_dos_device_name(segment))
            return {};
        target /= segment;
        any = true;
    }
    if (!any)
        return {};
    // Defence in depth: the normalized target must still be below the normalized root.
    const fs::path normal_root   = root.lexically_normal();
    const fs::path normal_target = target.lexically_normal();
    auto r = normal_root.begin();
    auto t = normal_target.begin();
    for (; r != normal_root.end(); ++r, ++t) {
        if (t == normal_target.end() || *r != *t)
            return {};
    }
    if (t == normal_target.end())
        return {};
    return target;
}

} // namespace

std::shared_ptr<HttpServer::Response> HttpServer::creality_handle_request(const std::string& url)
{
    BOOST_LOG_TRIVIAL(trace) << "creality server: get_response:" << url;
    std::string path;
    std::string params;
    parse_url(url, path, params);

    try {
        fs::path file_path;
        if (path.compare(0, 15, "/printer-cover/") == 0) {
            const std::string cover = printer_cover_route_path(path, data_dir(), resources_dir());
            if (!cover.empty())
                file_path = fs::path(cover);
        } else if (path.compare(0, 11, "/resources/") == 0) {
            file_path = static_file_path(fs::path(resources_dir()), path.substr(11));
        } else {
            file_path = static_file_path(fs::path(resources_dir()) / "web", path == "/" ? std::string("index.html") : path);
        }
        if (file_path.empty())
            return std::make_shared<ResponseNotFound>();

        if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
            // 文件不存在或者不是常规文件，返回404错误
            return std::make_shared<ResponseNotFound>();
        }

        boost::nowide::ifstream file(file_path.string(), std::ios::binary);
        if (!file)
            return std::make_shared<ResponseNotFound>();
        std::string file_content = std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

        std::string file_extension = fs::extension(file_path);
        std::string content_type = get_content_type(file_extension);
        std::string http_response = build_http_response(200, content_type, file_content);

        return std::make_shared<ResponseStaticFile>(http_response);
    } catch (std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "creality server: exception while serving a file: " << e.what();
        return std::make_shared<ResponseNotFound>();
    }
}
void HttpServer::ResponseStaticFile::write_response(std::stringstream& ssOut)
{
    ssOut << m_http_response;
}
void HttpServer::ResponseNotFound::write_response(std::stringstream& ssOut)
{
    const std::string sHTML = "<html><body><h1>404 Not Found</h1><p>There's nothing here.</p></body></html>";
    ssOut << "HTTP/1.1 404 Not Found" << std::endl;
    ssOut << "content-type: text/html" << std::endl;
    ssOut << "content-length: " << sHTML.length() << std::endl;
    ssOut << "connection: close" << std::endl;
    ssOut << std::endl;
    ssOut << sHTML;
}

void HttpServer::ResponseRedirect::write_response(std::stringstream& ssOut)
{
    const std::string sHTML = "<html><body><p>redirect to url </p></body></html>";
    ssOut << "HTTP/1.1 302 Found" << std::endl;
    ssOut << "Location: " << location_str << std::endl;
    ssOut << "content-type: text/html" << std::endl;
    ssOut << "content-length: " << sHTML.length() << std::endl;
    ssOut << "connection: close" << std::endl;
    ssOut << std::endl;
    ssOut << sHTML;
}

} // GUI
} //Slic3r
