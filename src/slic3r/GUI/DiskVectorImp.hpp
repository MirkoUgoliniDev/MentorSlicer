#ifndef slic3r_GUI_DiskVectorImp_hpp_
#define slic3r_GUI_DiskVectorImp_hpp_

#include <vector>
#include <string>
#include <memory>

#define SIZE_OF_MEM_THRESHOLD        307200

namespace Slic3r {

struct DiskVectorImp
{
    DiskVectorImp();
    ~DiskVectorImp();

    DiskVectorImp(DiskVectorImp&&);

    DiskVectorImp(unsigned int count) { m_memory_buffer.resize(count); };
    size_t                size() const;
    size_t                capacity() const;
    unsigned short&       operator[](const size_t _Pos);
    void                  push_back(const unsigned short& _Val); 
    //void                  emplace_back(const unsigned short& _Val);
    void                  shrink_to_fit() { m_memory_buffer.shrink_to_fit(); };
    const unsigned short* data() const;
    unsigned short*       data();
	void clear();

	// load data in chunks, size by element
	using ChunkCallback = std::function<void(const std::vector<unsigned short>& chunk, const int offset, const int elements_size)>;
	
	bool loop_load_chunks(ChunkCallback callback);

	//load all data once to dst, size by element
    size_t load_all_data(std::vector<unsigned short>& dst);

	void flush_memory_and_close();

protected:
	void check_and_flush();
	void flush_memory_to_disk();

private:
    std::vector<unsigned short> m_memory_buffer;
    std::string                 m_disk_filename;
    std::fstream                m_disk_file;
    size_t                      m_memory_threshold{SIZE_OF_MEM_THRESHOLD}; // no with bytes, by elements' size
    size_t                      m_total_elements{0};
    bool                        m_disk_initialized{false};

};

} // namespace Slic3r

#endif // slic3r_GUI_DiskVectorImp_hpp_