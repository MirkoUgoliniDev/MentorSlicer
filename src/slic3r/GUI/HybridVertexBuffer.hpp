#ifndef slic3r_GUI_HybridVertexVector_hpp_
#define slic3r_GUI_HybridVertexVector_hpp_

#include <vector>

namespace Slic3r {

struct HybridVertexVector {

	HybridVertexVector(bool use_short_type = true);
    ~HybridVertexVector();

    HybridVertexVector(HybridVertexVector&& other);
    HybridVertexVector(const HybridVertexVector& other);

    HybridVertexVector(unsigned int count);

    HybridVertexVector& operator=(const HybridVertexVector& other);
    HybridVertexVector& operator=(HybridVertexVector&& other);

    size_t          size() const;
    size_t          capacity() const;
    bool            empty() const;

	void store_data_at_index(const size_t _Pos, const float& _Val);

    //float& operator[](const size_t _Pos);
    float operator[](const size_t _Pos) const; 

	size_t size_of_element_type() const;

	//void   push_back(const short& _Val);
    void   push_back(const float& _Val);
    void                  emplace_back(const unsigned short& _Val);
    void                  shrink_to_fit();
    //void                  reserve(size_t count);
    const void* data() const;
    
 #if 0
	void  clear();
    float front() const;
    float& front();
    float back() const;
    float& back();
#endif

private:
    const bool         m_use_short_type;
    std::vector<short> m_vector_s;
    std::vector<float> m_vector_f;

};


}

#endif  //slic3r_GUI_HybridVertexVector_hpp_