#include "HybridIndexBuffer.hpp"

namespace Slic3r {

HybridIndexBuffer::HybridIndexBuffer(bool enable_hybrid)
	:m_enable_hybrid(enable_hybrid) 
{
}

HybridIndexBuffer::~HybridIndexBuffer() 
{	
}

HybridIndexBuffer::HybridIndexBuffer(HybridIndexBuffer&& other) 
	: m_enable_hybrid(other.m_enable_hybrid)
	, m_mem_vector(std::move(other.m_mem_vector))
	, m_disk_vector(std::move(other.m_disk_vector))
{
}

HybridIndexBuffer::HybridIndexBuffer(unsigned int count) 
: m_enable_hybrid(false) 
{
	m_mem_vector.resize(count);
}


size_t HybridIndexBuffer::size() const {
    if (m_enable_hybrid) {
        return m_disk_vector.size();
    } else {
        return m_mem_vector.size();
    }
}

size_t HybridIndexBuffer::capacity() const 
{ 
	if (m_enable_hybrid) {
        return m_disk_vector.capacity();
    } else {
        return m_mem_vector.capacity();
    }
}

unsigned short& HybridIndexBuffer::operator[](const size_t _Pos) { 
	if (m_enable_hybrid) {
		//This shall not happen
        return m_disk_vector.operator[](_Pos);
    } else {
        return m_mem_vector.operator[](_Pos);
    }
 }

void HybridIndexBuffer::push_back(const unsigned short& _Val)
{	
	if (m_enable_hybrid) {
        return m_disk_vector.push_back(_Val);
    } else {
        return m_mem_vector.push_back(_Val);
    }
}


void HybridIndexBuffer::shrink_to_fit() 
{
    if (m_enable_hybrid) {
        return m_disk_vector.shrink_to_fit();
    } else {
        return m_mem_vector.shrink_to_fit();
    }
}

const unsigned short* HybridIndexBuffer::data() const
{ 
	if (m_enable_hybrid) {
        return m_disk_vector.data();
    } else {
        return m_mem_vector.data();
    }
}

unsigned short* HybridIndexBuffer::data()
{
    if (m_enable_hybrid) {
        return m_disk_vector.data();
    } else {
        return m_mem_vector.data();
    }
}

void HybridIndexBuffer::clear()
{
    if (m_enable_hybrid) {
        m_disk_vector.clear();
    } else {
        m_mem_vector.clear();
    }
}

size_t HybridIndexBuffer::load_all_data(std::vector<unsigned short>& dst) {

	if (m_enable_hybrid) {
        return m_disk_vector.load_all_data(dst);
    }

    return 0;
}

bool HybridIndexBuffer::loop_load_chunks(ChunkCallback callback) 
{
	if (m_enable_hybrid) {
        return m_disk_vector.loop_load_chunks(callback);
    }

	return true;
}

void HybridIndexBuffer::flush_memory_and_close() 
{ 
	if (m_enable_hybrid) {
        m_disk_vector.flush_memory_and_close();
    }
}


} // namespace Slic3r