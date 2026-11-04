#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace datapump::detail {
// Ordinary receivers keep their original contiguous representation. Sparse
// acquisition owns only materialized tracks: growing the pointer directory
// never moves an owner borrowed by publication or guard replay.
template<class T> class StableOwnerStore {
    struct Owner : T { std::size_t slot=0; };
    using Pointer=std::unique_ptr<Owner>;
    std::vector<T> dense_;
    std::unique_ptr<Pointer[]> owners_;
    std::size_t size_=0,capacity_=0;
    bool indirect_=false;
    static constexpr std::size_t allocation_allowance=64;
    static std::size_t multiply(std::size_t n,std::size_t width) {
        if(n>std::numeric_limits<std::size_t>::max()/width)throw std::length_error("stable owner storage overflow");
        return n*width;
    }
    static std::size_t add(std::size_t a,std::size_t b) {
        if(b>std::numeric_limits<std::size_t>::max()-a)throw std::length_error("stable owner storage overflow");
        return a+b;
    }
    std::size_t next_capacity(std::size_t n)const {
        if(n<=capacity_)return capacity_;
        const auto doubled=capacity_>std::numeric_limits<std::size_t>::max()/2?n:capacity_*2;
        return std::max(n,doubled);
    }
public:
    // Includes amortized directory capacity and allocation allowances. The
    // old directory coexists during growth; growth_peak_bytes charges it via
    // the current working set plus the full new directory.
    static constexpr std::size_t indirect_slot_bound=sizeof(Owner)+2*sizeof(Pointer)+2*allocation_allowance;
    void use_indirect() {
        if(!dense_.empty() || dense_.capacity())throw std::logic_error("stable owner mode changed after allocation");
        indirect_=true;
    }
    std::size_t size()const noexcept{return indirect_?size_:dense_.size();}
    bool empty()const noexcept{return size()==0;}
    std::size_t capacity()const noexcept{return indirect_?capacity_:dense_.capacity();}
    // Dense diagnostic references retain their original contiguous identity.
    // Sparse ownership must never masquerade as a contiguous payload array.
    T* data() {
        if(indirect_)throw std::logic_error("sparse owner storage is not contiguous");
        return dense_.data();
    }
    const T* data()const {
        if(indirect_)throw std::logic_error("sparse owner storage is not contiguous");
        return dense_.data();
    }
    std::size_t storage_bytes()const {
        if(!indirect_)return multiply(dense_.capacity(),sizeof(T));
        return add(multiply(size_,sizeof(Owner)+allocation_allowance),
            capacity_?add(multiply(capacity_,sizeof(Pointer)),allocation_allowance):0);
    }
    std::size_t growth_peak_bytes(std::size_t n)const {
        if(!indirect_ || n<=size_)return 0;
        auto bytes=multiply(n-size_,sizeof(Owner)+allocation_allowance);
        if(n>capacity_)bytes=add(bytes,add(multiply(next_capacity(n),sizeof(Pointer)),allocation_allowance));
        return bytes;
    }
    void reserve(std::size_t n) {
        if(!indirect_){dense_.reserve(n);return;}
        if(n<=capacity_)return;
        const auto count=next_capacity(n);(void)multiply(count,sizeof(Pointer));
        auto replacement=std::make_unique<Pointer[]>(count);
        for(std::size_t i=0;i<size_;++i)replacement[i]=std::move(owners_[i]);
        owners_=std::move(replacement);capacity_=count;
    }
    void resize(std::size_t n) {
        if(!indirect_){dense_.resize(n);return;}
        reserve(n);
        const auto old=size_;
        try {
            while(size_<n){auto owner=std::make_unique<Owner>();owner->slot=size_;owners_[size_]=std::move(owner);++size_;}
        } catch(...) {
            while(size_>old)owners_[--size_].reset();
            throw;
        }
        while(size_>n)owners_[--size_].reset();
    }
    void push_back(T value) {
        if(!indirect_){dense_.push_back(std::move(value));return;}
        const auto at=size_;resize(add(size_,1));(*this)[at]=std::move(value);
    }
    T& operator[](std::size_t i)noexcept {return indirect_?static_cast<T&>(*owners_[i]):dense_[i];}
    const T& operator[](std::size_t i)const noexcept{return indirect_?static_cast<const T&>(*owners_[i]):dense_[i];}
    T& back()noexcept{return (*this)[size()-1];}
    const T& back()const noexcept{return (*this)[size()-1];}
    std::size_t index(const T& value)const noexcept {
        if(!indirect_)return static_cast<std::size_t>(&value-dense_.data());
        const auto at=static_cast<const Owner&>(value).slot;
        assert(at<size_ && &(*this)[at]==&value);return at;
    }
    template<bool Constant> class Iterator {
        using Store=std::conditional_t<Constant,const StableOwnerStore,StableOwnerStore>;
        Store* store_=nullptr;std::size_t position_=0;
    public:
        using value_type=T;using difference_type=std::ptrdiff_t;
        using reference=std::conditional_t<Constant,const T&,T&>;
        using pointer=std::conditional_t<Constant,const T*,T*>;
        using iterator_category=std::forward_iterator_tag;
        Iterator()=default;
        Iterator(Store* s,std::size_t p):store_(s),position_(p){}
        reference operator*()const{return (*store_)[position_];}
        pointer operator->()const{return &**this;}
        Iterator& operator++(){++position_;return *this;}
        Iterator operator++(int){auto old=*this;++*this;return old;}
        bool operator==(const Iterator&)const=default;
    };
    Iterator<false> begin(){return {this,0};}
    Iterator<false> end(){return {this,size()};}
    Iterator<true> begin()const{return {this,0};}
    Iterator<true> end()const{return {this,size()};}
};
}
