#ifndef slic3r_GUI_HybridVector_hpp_
#define slic3r_GUI_HybridVector_hpp_

#include <vector>
#include "DiskVectorImp.hpp"

namespace Slic3r {

// size by element
    using ChunkCallback = std::function<void(const std::vector<unsigned short>& chunk, const int offset, const int elements_size)>;

struct HybridIndexBuffer
{
    HybridIndexBuffer(bool enable_hybrid = false);
    ~HybridIndexBuffer();

    HybridIndexBuffer(HybridIndexBuffer&&);

    //HybridIndexBuffer(const size_t count);
    HybridIndexBuffer(unsigned int count);
    
    size_t          size() const;
    size_t          capacity() const;
    unsigned short& operator[](const size_t _Pos);
    void            push_back(const unsigned short& _Val);
    // void                  emplace_back(const unsigned short& _Val);
    void                  shrink_to_fit();
    const unsigned short* data() const;
    unsigned short*       data();
    void                  clear();

    void flush_memory_and_close();	

    bool loop_load_chunks(ChunkCallback callback);

	size_t load_all_data(std::vector<unsigned short>& dst);

private:
    const bool                  m_enable_hybrid;
	std::vector<unsigned short> m_mem_vector;
    DiskVectorImp m_disk_vector;
};

}

#endif //slic3r_GUI_HybridVector_hpp_
