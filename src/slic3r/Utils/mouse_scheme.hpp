#pragma once
#include <string>

namespace Slic3r { namespace GUI {
void send_message_mac(const std::string& msg, const std::string& instance_id);
void register_receive_mac(void (*handler)(const std::string&));
}} // namespace Slic3r::GUI
