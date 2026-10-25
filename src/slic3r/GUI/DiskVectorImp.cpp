#include "DiskVectorImp.hpp"
#include <algorithm>

#include <boost/filesystem.hpp>
//#include <boost/archive/binary_oarchive.hpp>
//#include <boost/archive/binary_iarchive.hpp>
#include <boost/serialization/vector.hpp>

#include <fstream>
#include <sstream>

#include "libslic3r/Utils.hpp"

static int token = 0;

#define BUFFER_DIR "/creality_tmp/"

namespace Slic3r {

DiskVectorImp::DiskVectorImp() {
	
	token ++;
    std::string save_dir = temporary_dir() + BUFFER_DIR;
    m_disk_filename = save_dir + "vhb_data_" + std::to_string(token) + ".bin";

	if (m_memory_threshold > 0)
		m_memory_buffer.reserve(m_memory_threshold);
}

DiskVectorImp::~DiskVectorImp() {
	clear();
}

DiskVectorImp::DiskVectorImp(DiskVectorImp&& other)
    : m_memory_buffer(std::move(other.m_memory_buffer))
    , m_disk_filename(std::move(other.m_disk_filename))
    , m_memory_threshold(other.m_memory_threshold)
    , m_total_elements(other.m_total_elements)
    , m_disk_initialized(other.m_disk_initialized)
{
}

size_t DiskVectorImp::size() const {
	return m_total_elements;
}

size_t DiskVectorImp::capacity() const { 
	return m_total_elements;
}

unsigned short& DiskVectorImp::operator[](const size_t _Pos) { 
	static unsigned short error = -1;
	if (_Pos < m_total_elements)
	{
        int disk_element = m_total_elements - m_memory_buffer.size();
        if (_Pos < disk_element) {
			//read from file
			
			bool open = m_disk_file.is_open();
            if (!open) {
                if (boost::filesystem::exists(m_disk_filename))
                    m_disk_file.open(m_disk_filename, std::ios::binary | std::ios::in | std::ios::out | std::ios::ate);
                if (!m_disk_file.is_open()) {
                    return error;
                }

				m_disk_file.seekg(_Pos, std::ios::beg);
                static unsigned short result = 0;
                m_disk_file.read(reinterpret_cast<char*>(&result), sizeof(unsigned short));
                if (m_disk_file.fail()) {
                    return error;
                }
				return result;
			}
		} else {
            return m_memory_buffer.operator[](_Pos - disk_element);
		}
	}

	assert(0);
    return error;
 }

void DiskVectorImp::push_back(const unsigned short& _Val)
{	
	m_memory_buffer.push_back(_Val); 
	m_total_elements++;
	check_and_flush();
}

//void DiskVectorImp::emplace_back(const unsigned short& _Val) 
//{
//	m_memory_buffer.emplace_back(_Val); 
//	m_total_elements++;
//	check_and_flush();
//}

const unsigned short* DiskVectorImp::data() const 
{ 
	// should never call
	assert(false);
	return m_memory_buffer.data(); 
}

unsigned short* DiskVectorImp::data()
{
    assert(false); //should never call
    return m_memory_buffer.data();
}

void DiskVectorImp::check_and_flush()
{
    if (m_memory_buffer.size() >= m_memory_threshold) {
        flush_memory_to_disk();
    }
}

void DiskVectorImp::flush_memory_to_disk()
{
    if (m_memory_buffer.empty()) {
        return;
    }

    try {
        namespace fs = boost::filesystem;

		fs::path file_path(m_disk_filename);
        fs::path parent_path = file_path.parent_path();
        
        if (!parent_path.empty() && !fs::exists(parent_path)) {
            fs::create_directories(parent_path);
        }
        
		const std::string& full_path = m_disk_filename;

		auto length = sizeof(unsigned short) * m_memory_buffer.size();

		if (!m_disk_initialized) {
            
			if (boost::filesystem::exists(full_path))
                boost::filesystem::remove(full_path);

            m_disk_file.open(full_path, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);

            m_disk_initialized = true;
        }
        
		/*auto sp = m_disk_file.tellp();
        auto sg = m_disk_file.tellg();*/

		m_disk_file.seekp(0, std::ios::end);
		m_disk_file.write(reinterpret_cast<const char*>(m_memory_buffer.data()), length);
        m_disk_file.flush();

		m_memory_buffer.clear();

    } catch (const std::exception& e) {
        throw std::runtime_error(__FUNCTION__ + std::string(e.what()));
    }
}

void DiskVectorImp::clear()
{
    m_memory_buffer.clear();
    m_memory_buffer.shrink_to_fit();
    m_total_elements = 0;

	if (m_disk_file.is_open()) {
        m_disk_file.close();
	}

	if (m_disk_initialized && boost::filesystem::exists(m_disk_filename)) {
        boost::filesystem::remove(m_disk_filename);
        m_disk_initialized = false;
    }
}

bool DiskVectorImp::loop_load_chunks(ChunkCallback callback)
{
    size_t acc = 0;
	size_t mem_buf_size = m_memory_buffer.size();
    if (mem_buf_size > 0) 
	{
        callback(m_memory_buffer, m_total_elements - mem_buf_size, mem_buf_size);
        acc += mem_buf_size;
	}

	if (m_disk_initialized == false)
		return true;
	
	bool good = m_disk_file.good();
    bool open = m_disk_file.is_open();
    if (!open) {
        if (boost::filesystem::exists(m_disk_filename))
            m_disk_file.open(m_disk_filename, std::ios::binary | std::ios::in | std::ios::out | std::ios::ate);
        if (!m_disk_file.is_open()) {
            return false;
        }
	}
	m_disk_file.seekg(0, std::ios::end);
    long disk_file_size = m_disk_file.tellg() ;
    if (disk_file_size <= 0) {
		return false;
	}

    long disk_elements = disk_file_size / sizeof(unsigned short);
    if (disk_elements + mem_buf_size != m_total_elements) {
        return false;
	}

	// flush mem buffer to file
	//m_disk_file.seekp(0, std::ios::end);
    m_disk_file.write(reinterpret_cast<const char*>(m_memory_buffer.data()), m_memory_buffer.size() * sizeof(unsigned short));
    //m_disk_file.flush();
    m_memory_buffer.clear();

    int chunk_offset = 0;
    std::vector<unsigned short>& batch = m_memory_buffer;
    batch.resize(m_memory_threshold);

	m_disk_file.seekg(0, std::ios::beg);
    
    while (acc < m_total_elements) {
        size_t elements_to_read = m_total_elements - acc >= m_memory_threshold ? m_memory_threshold : m_total_elements - acc;
        m_disk_file.read(reinterpret_cast<char*>(batch.data()), elements_to_read * sizeof(unsigned short));
        std::streamsize bytes_read = m_disk_file.gcount();
        auto chunk_size = bytes_read / sizeof(unsigned short);
        
		if (m_disk_file.fail()) {
            return false;
        }		

		callback(batch, chunk_offset, chunk_size);
		
        chunk_offset += chunk_size;
        acc += chunk_size;
        
		if (m_disk_file.eof()) {
            break;
        }
	}

	batch.clear();	
    batch.shrink_to_fit();

	if (m_disk_file.is_open()) {
        m_disk_file.close();
    }

	if (acc != m_total_elements) {        
        return false;
	}

    return true;

}

size_t DiskVectorImp::load_all_data(std::vector<unsigned short>& dst)
{
    const size_t mem_buf_size = m_memory_buffer.size();
    if (mem_buf_size == m_total_elements || m_disk_initialized == false) {
        if (dst.size() < mem_buf_size)
            dst.resize(mem_buf_size);

        memcpy(dst.data(), m_memory_buffer.data(), mem_buf_size * sizeof(unsigned short));
        return mem_buf_size;
    }

    // bool good = m_disk_file.good();
    bool open = m_disk_file.is_open();
    if (!open) {
        if (boost::filesystem::exists(m_disk_filename))
            m_disk_file.open(m_disk_filename, std::ios::binary | std::ios::in | std::ios::out | std::ios::ate);
        if (!m_disk_file.is_open()) {
            return 0;
        }
    }
    m_disk_file.seekg(0, std::ios::end);
    long disk_file_size = m_disk_file.tellg();
    if (disk_file_size <= 0) {
        return 0;
    }

    long disk_elements = disk_file_size / sizeof(unsigned short);
    if (disk_elements + mem_buf_size != m_total_elements) {
        return 0;
    }

    if (dst.size() < m_total_elements)
        dst.resize(m_total_elements);

    m_disk_file.seekg(0, std::ios::beg);

    m_disk_file.read(reinterpret_cast<char*>(dst.data()), disk_file_size);

    if (m_disk_file.fail()) {
        m_disk_file.close();
        return 0;
    }

    std::streamsize bytes_read = m_disk_file.gcount();
    if (disk_file_size != bytes_read) {
        m_disk_file.close();
        return 0;
    }
    m_disk_file.close();

    memcpy((char*) (dst.data()) + disk_file_size, m_memory_buffer.data(), mem_buf_size * sizeof(unsigned short));

    return m_total_elements;
}

void DiskVectorImp::flush_memory_and_close() { 

	flush_memory_to_disk();
    if (m_disk_file.is_open()) {
        m_disk_file.close();
    }
}

} // namespace Slic3r