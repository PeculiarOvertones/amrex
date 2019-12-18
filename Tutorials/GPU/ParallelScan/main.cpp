
#include <AMReX.H>
#include <AMReX_ParmParse.H>
#include <AMReX_Scan.H>

#include <thrust/device_malloc_allocator.h>
#include <thrust/scan.h>

namespace amrex
{
    template<class T>
    class ThrustManagedAllocator : public thrust::device_malloc_allocator<T>
    {
    public:
        using value_type = T;
        
        typedef thrust::device_ptr<T>  pointer;
        inline pointer allocate(size_t n)
        {
            value_type* result = nullptr;
            result = (value_type*) The_Arena()->alloc(n * sizeof(T));
            return thrust::device_pointer_cast(result);
        }
        
        inline void deallocate(pointer ptr, size_t)
        {
            The_Arena()->free(thrust::raw_pointer_cast(ptr));
        }
    };

    namespace
    {
        ThrustManagedAllocator<char> g_cached_allocator;
    }

    namespace Gpu
    {
        ThrustManagedAllocator<char>& The_ThrustCachedAllocator () { return g_cached_allocator; };
        
        AMREX_FORCE_INLINE auto The_ThrustCachedPolicy() -> decltype (thrust::cuda::par(Gpu::The_ThrustCachedAllocator()))
        {
            return thrust::cuda::par(Gpu::The_ThrustCachedAllocator());
        };
    }
}

using namespace amrex;

void main_main();

int main (int argc, char* argv[])
{
    amrex::Initialize(argc,argv);
    main_main();
    amrex::Finalize();
}

void main_main ()
{
    long N = 256*1024*1024 - 37;
    {
        ParmParse pp;
        pp.query("n", N);
    }

    typedef int T;
    Vector<T> h_in(N);
    for (auto& x: h_in) {
        x = static_cast<T>((Random()-0.5)*100.);
    }
    Vector<T> h_inclusive_thrust(N);
    Vector<T> h_inclusive_amrex(N);
    Vector<T> h_exclusive_thrust(N);
    Vector<T> h_exclusive_amrex(N);

    std::size_t nbytes = h_in.size()*sizeof(T);
    T* d_in = (T*)The_Device_Arena()->alloc(nbytes);
    T* d_out = (T*)The_Device_Arena()->alloc(nbytes);

    amrex::Gpu::htod_memcpy(d_in, h_in.data(), nbytes);

    // warm up
    Scan::InclusiveSum(N, d_in, d_out);
    Scan::ExclusiveSum(N, d_in, d_out);
    Gpu::synchronize();
    thrust::inclusive_scan(Gpu::The_ThrustCachedPolicy(),
                           d_in, d_in+N, d_out);
    thrust::exclusive_scan(Gpu::The_ThrustCachedPolicy(),
                           d_in, d_in+N, d_out);

    {
        BL_PROFILE("amrex::inclusive_scan");
        Scan::InclusiveSum(N, d_in, d_out);
        Gpu::synchronize();
    }
    amrex::Gpu::dtoh_memcpy(h_inclusive_amrex.data(), d_out, nbytes);

    {
        BL_PROFILE("amrex::exclusive_scan");
        Scan::ExclusiveSum(N, d_in, d_out);
        Gpu::synchronize();
    }
    amrex::Gpu::dtoh_memcpy(h_exclusive_amrex.data(), d_out, nbytes);

    {
        BL_PROFILE("thrust::inclusive_scan");
        thrust::inclusive_scan(Gpu::The_ThrustCachedPolicy(),
                               d_in, d_in+N, d_out);
        Gpu::synchronize();
    }
    amrex::Gpu::dtoh_memcpy(h_inclusive_thrust.data(), d_out, nbytes);

    {
        BL_PROFILE("thrust::exclusive_scan");
        thrust::exclusive_scan(Gpu::The_ThrustCachedPolicy(),
                               d_in, d_in+N, d_out);
        Gpu::synchronize();
    }
    amrex::Gpu::dtoh_memcpy(h_exclusive_thrust.data(), d_out, nbytes);

    for (int i = 0; i < N; ++i) {
        if (h_inclusive_thrust[i] != h_inclusive_amrex[i]) {
            amrex::Print() << "i = " << i << ", thrust = " << h_inclusive_thrust[i]
                           << ", amrex = " << h_inclusive_amrex[i] << "\n"; 
            amrex::Abort("inclusive scan failed");
        }
        if (h_exclusive_thrust[i] != h_exclusive_amrex[i]) {
            amrex::Print() << "i = " << i << ", thrust = " << h_exclusive_thrust[i]
                           << ", amrex = " << h_exclusive_amrex[i] << "\n"; 
            amrex::Abort("exclusive scan failed");
        }
    }

    The_Device_Arena()->free(d_in);
    The_Device_Arena()->free(d_out);
}
