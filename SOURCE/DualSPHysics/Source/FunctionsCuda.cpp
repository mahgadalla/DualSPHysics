//HEAD_DSCODES
/*
 <DUALSPHYSICS>  Copyright (c) 2017 by Dr Jose M. Dominguez et al. (see http://dual.sphysics.org/index.php/developers/). 

 EPHYSLAB Environmental Physics Laboratory, Universidade de Vigo, Ourense, Spain.
 School of Mechanical, Aerospace and Civil Engineering, University of Manchester, Manchester, U.K.

 This file is part of DualSPHysics. 

 DualSPHysics is free software: you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License 
 as published by the Free Software Foundation; either version 2.1 of the License, or (at your option) any later version.
 
 DualSPHysics is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details. 

 You should have received a copy of the GNU Lesser General Public License along with DualSPHysics. If not, see <http://www.gnu.org/licenses/>. 
*/

/// \file FunctionsCuda.cpp \brief Declares basic/general GPU functions for the entire application.

#include "FunctionsCuda.h"
#include "Functions.h"
#include <algorithm>

#pragma warning(disable : 4996) //Cancels sprintf() deprecated.

namespace fcuda{

//==============================================================================
/// Returns information about selected GPU (code from deviceQuery example).
//==============================================================================
inline bool IsGPUCapableP2P(const cudaDeviceProp *pProp){
#ifdef _WIN32
    return(pProp->major>=2 && pProp->tccDriver? true: false);
#else
    return(pProp->major >= 2);
#endif
}

//==============================================================================
/// Returns name about selected GPU.
//==============================================================================
std::string GetCudaDeviceName(int gid){
  cudaSetDevice(gid);
  CheckCudaErrors("Failed selecting device.");
  cudaDeviceProp deviceProp;
  cudaGetDeviceProperties(&deviceProp,gid);
  CheckCudaErrors("Failed getting selected device info.");
  return(deviceProp.name);
}

//==============================================================================
/// Returns information about selected GPU (code from deviceQuery example).
//==============================================================================
StGpuInfo GetCudaDeviceInfo(int gid){
  cudaSetDevice(gid);
  CheckCudaErrors("Failed selecting device.");
  cudaDeviceProp deviceProp;
  cudaGetDeviceProperties(&deviceProp,gid);
  CheckCudaErrors("Failed getting selected device info.");
  StGpuInfo g;
  g.id=gid;
  g.name=deviceProp.name;
  g.ccmajor=deviceProp.major;
  g.ccminor=deviceProp.minor;
  g.globalmem=deviceProp.totalGlobalMem;
  g.mp=deviceProp.multiProcessorCount;
  g.coresmp=_ConvertSMVer2Cores(deviceProp.major,deviceProp.minor);
  g.cores=g.coresmp*g.mp;
  g.clockrate=deviceProp.clockRate;
#if CUDART_VERSION >= 5000
  g.clockratemem=deviceProp.memoryClockRate;
  g.busmem=deviceProp.memoryBusWidth;
  g.cachelv2=deviceProp.l2CacheSize;
#endif
  g.constantmem=deviceProp.totalConstMem;
  g.sharedmem=deviceProp.sharedMemPerBlock;
  g.regsblock=deviceProp.regsPerBlock;
  g.maxthmp=deviceProp.maxThreadsPerMultiProcessor;
  g.maxthblock=deviceProp.maxThreadsPerBlock;
  g.overlap=deviceProp.deviceOverlap;
  g.overlapcount=deviceProp.asyncEngineCount;
  g.limitrun=deviceProp.kernelExecTimeoutEnabled;
  g.integrated=deviceProp.integrated;
  g.maphostmem=deviceProp.canMapHostMemory;
  g.eccmode=deviceProp.ECCEnabled;
#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
  g.tccdriver=deviceProp.tccDriver;
#endif
  g.uva=deviceProp.unifiedAddressing;
  g.pcidomain=deviceProp.pciDomainID;
  g.pcibus=deviceProp.pciBusID;
  g.pcidevice=deviceProp.pciDeviceID;
#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
  g.rdma=(g.ccmajor>=2 && g.tccdriver); // on Windows (64-bit), the Tesla Compute Cluster driver for windows must be enabled to support this
#else
  g.rdma=(g.ccmajor>=2);
#endif
  //-Check possibility for peer access.
  //:printf("------->Check possibility for peer access.\n");
  if(g.rdma){
    int deviceCount=0;
    cudaGetDeviceCount(&deviceCount);
    CheckCudaErrors("Failed getting devices info.");
    g.countp2pto=0;
    for(int cg=0;cg<deviceCount;cg++)if(cg!=gid){
      int can_access_peer;
      cudaDeviceCanAccessPeer(&can_access_peer,gid,cg);
      if(can_access_peer){
        if(g.countp2pto>=g.sizep2pto)throw "StGpuInfo.sizep2pto is not enough.";
        g.p2pto[g.countp2pto++]=cg;
      }
    }
    int count2=0;
    for(int cg=0;cg<deviceCount;cg++)if(cg!=gid){
      int can_access_peer;
      cudaDeviceCanAccessPeer(&can_access_peer,cg,gid);
      if(can_access_peer && (count2>=g.sizep2pto || g.p2pto[count2++]!=cg))
        throw "There is no agreement between to and from peer access.";
    }
  }
  return(g);
}

//==============================================================================
/// Returns information about detected GPUs (code from deviceQuery example).
//==============================================================================
int GetCudaDevicesInfo(std::vector<std::string> *gpuinfo,std::vector<StGpuInfo> *gpuprops){
  if(gpuinfo)gpuinfo->push_back("[CUDA Capable device(s)]");
  int deviceCount=0;
  cudaGetDeviceCount(&deviceCount);
  CheckCudaErrors("Failed getting devices info.");
  if(gpuinfo){
    if(!deviceCount)gpuinfo->push_back("  There are no available device(s) that support CUDA");
    else gpuinfo->push_back(fun::PrintStr("  Detected %d CUDA Capable device(s)",deviceCount));
  }
  int gid0=-10; cudaGetDevice(&gid0);
  //-Driver information.
  int driverVersion=0,runtimeVersion=0;
  cudaDriverGetVersion(&driverVersion);
  cudaRuntimeGetVersion(&runtimeVersion);
  if(gpuinfo)gpuinfo->push_back(fun::PrintStr("  CUDA Driver Version / Runtime Version: %d.%d / %d.%d",driverVersion/1000,(driverVersion%100)/10,runtimeVersion/1000,(runtimeVersion%100)/10));
  //-Devices information.
  for(int dev=0;dev<deviceCount;++dev){
    const StGpuInfo g=GetCudaDeviceInfo(dev);
    if(gpuinfo){
      gpuinfo->push_back(" ");
      gpuinfo->push_back(fun::PrintStr("Device %d: \"%s\"",dev,g.name.c_str()));
      gpuinfo->push_back(fun::PrintStr("  CUDA Capability Major....: %d.%d",g.ccmajor,g.ccminor));
      gpuinfo->push_back(fun::PrintStr("  Global memory............: %.0f MBytes",(float)g.globalmem/1048576.0f));
      gpuinfo->push_back(fun::PrintStr("  CUDA Cores...............: %d (%2d Multiprocessors, %3d CUDA Cores/MP)",g.cores,g.mp,g.coresmp));
      gpuinfo->push_back(fun::PrintStr("  GPU Max Clock rate.......: %.0f MHz (%0.2f GHz)",1e-3f*g.clockrate,1e-6f*g.clockrate));
#if CUDART_VERSION >= 5000
      gpuinfo->push_back(fun::PrintStr("  Memory Clock rate........: %.0f Mhz",1e-3f*g.clockratemem));
      gpuinfo->push_back(fun::PrintStr("  Memory Bus Width.........: %d-bit",g.busmem));
      gpuinfo->push_back(fun::PrintStr("  L2 Cache Size............: %.0f KBytes",(float)g.cachelv2/1024.f));
#endif
      gpuinfo->push_back(fun::PrintStr("  Constant memory..........: %.0f KBytes",(float)g.constantmem/1024.f));
      gpuinfo->push_back(fun::PrintStr("  Shared memory per block..: %.0f KBytes",(float)g.sharedmem/1024.f));
      gpuinfo->push_back(fun::PrintStr("  Registers per block......: %d",g.regsblock));
      gpuinfo->push_back(fun::PrintStr("  Maximum threads per MP...: %d",g.maxthmp));
      gpuinfo->push_back(fun::PrintStr("  Maximum threads per block: %d",g.maxthblock));
      gpuinfo->push_back(fun::PrintStr("  Concurrent copy and kernel execution....: %s with %d copy engine(s)",(g.overlap? "Yes": "No"),g.overlapcount));
      gpuinfo->push_back(fun::PrintStr("  Run time limit on kernels...............: %s",(g.limitrun? "Yes": "No")));
      gpuinfo->push_back(fun::PrintStr("  Integrated GPU sharing Host Memory......: %s",(g.integrated? "Yes": "No")));
      gpuinfo->push_back(fun::PrintStr("  Support host page-locked memory mapping.: %s",(g.maphostmem? "Yes": "No")));
      gpuinfo->push_back(fun::PrintStr("  Device has ECC support..................: %s",(g.eccmode? "Enabled": "Disabled")));
#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
      gpuinfo->push_back(fun::PrintStr("  CUDA Device Driver Mode (TCC or WDDM)...: %s",(g.tccdriver? "TCC (Tesla Compute Cluster Driver)": "WDDM (Windows Display Driver Model)")));
#endif
      gpuinfo->push_back(fun::PrintStr("  Device supports Unified Addressing (UVA): %s",(g.uva? "Yes": "No")));
      gpuinfo->push_back(fun::PrintStr("  Device PCI (Domain / Bus / location)....: %d / %d / %d",g.pcidomain,g.pcibus,g.pcidevice));
      gpuinfo->push_back(fun::PrintStr("  Device supports P2P and RDMA............: %s",(g.rdma? "Yes": "No")));
      if(g.rdma){
        std::string list;
        for(int c=0;c<g.countp2pto;c++)list=list+(list.empty()? "": ",")+fun::IntStr(g.p2pto[c]);
        gpuinfo->push_back(fun::PrintStr("  Device supports P2P from/to GPUs........: %s",list.c_str()));
      }
    }
    if(gpuprops)gpuprops->push_back(g);
  }
  int gid1=-10; cudaGetDevice(&gid1);
  if(gid0>=0 && gid0!=gid1)cudaSetDevice(gid0);
  return(deviceCount);
}

//==============================================================================
/// Returns cores per multiprocessor (code from helper_cuda.h).
//==============================================================================
int _ConvertSMVer2Cores(int major, int minor){
  /// Defines for GPU Architecture types (using the SM version to determine the # of cores per SM).
  typedef struct
  {
    int SM; // 0xMm (hexidecimal notation), M = SM Major version, and m = SM minor version
    int Cores;
  } sSMtoCores;

  sSMtoCores nGpuArchCoresPerSM[]=
  {
    { 0x20, 32 }, // Fermi Generation (SM 2.0) GF100 class
    { 0x21, 48 }, // Fermi Generation (SM 2.1) GF10x class
    { 0x30, 192}, // Kepler Generation (SM 3.0) GK10x class
    { 0x32, 192}, // Kepler Generation (SM 3.2) GK10x class
    { 0x35, 192}, // Kepler Generation (SM 3.5) GK11x class
    { 0x37, 192}, // Kepler Generation (SM 3.7) GK21x class
    { 0x50, 128}, // Maxwell Generation (SM 5.0) GM10x class
    { 0x52, 128}, // Maxwell Generation (SM 5.2) GM20x class
    { 0x53, 128}, // Maxwell Generation (SM 5.3) GM20x class
    { 0x60, 64 }, // Pascal Generation (SM 6.0) GP100 class
    { 0x61, 128}, // Pascal Generation (SM 6.1) GP10x class
    { 0x62, 128}, // Pascal Generation (SM 6.2) GP10x class
    { -1, -1 }
  };

  int index = 0;
  while (nGpuArchCoresPerSM[index].SM!=-1){
    if(nGpuArchCoresPerSM[index].SM==((major << 4)+minor))return(nGpuArchCoresPerSM[index].Cores);
    index++;
  }
  // If we don't find the values, we default use the previous one to run properly
  return(0);
}

//==============================================================================
/// Checks error and throws exception.
//==============================================================================
void CheckCudaError(const std::string &msg,const char *const file,int const line){
  cudaError_t err=cudaGetLastError();
  if(err!=cudaSuccess){
    const std::string tx=fun::PrintStr("%s (CUDA error at %s:%d code=%d(%s)).\n",msg.c_str(),file,line,err,cudaGetErrorString(err)); 
    throw tx;
  }
}


//##############################################################################
//## Functions to allocate GPU memory.
//##############################################################################

//==============================================================================
/// Allocates memory for word on GPU.
//==============================================================================
size_t Malloc(byte **ptr,unsigned count){
  size_t size=sizeof(byte)*count;  cudaMalloc((void**)ptr,size);  return(size);
}

//==============================================================================
/// Allocates memory for word on GPU.
//==============================================================================
size_t Malloc(word **ptr,unsigned count){
  size_t size=sizeof(word)*count;  cudaMalloc((void**)ptr,size);  return(size);
}

//==============================================================================
/// Allocates memory for unsigned on GPU.
//==============================================================================
size_t Malloc(unsigned **ptr,unsigned count){
  size_t size=sizeof(unsigned)*count;  cudaMalloc((void**)ptr,size);  return(size);
}

//==============================================================================
/// Allocates memory for int on GPU.
//==============================================================================
size_t Malloc(int **ptr,unsigned count){
  size_t size=sizeof(int)*count;  cudaMalloc((void**)ptr,size);  return(size);
}

//==============================================================================
/// Allocates memory for int2 on GPU.
//==============================================================================
size_t Malloc(int2 **ptr,unsigned count){
  size_t size=sizeof(int2)*count;  cudaMalloc((void**)ptr,size);  return(size);
}

//==============================================================================
/// Allocates memory for float on GPU.
//==============================================================================
size_t Malloc(float **ptr,unsigned count){
  size_t size=sizeof(float)*count;  cudaMalloc((void**)ptr,size);  return(size);
}

//==============================================================================
/// Allocates memory for float2 on GPU.
//==============================================================================
size_t Malloc(float2 **ptr,unsigned count){
  size_t size=sizeof(float2)*count;  cudaMalloc((void**)ptr,size);  return(size);
}

//==============================================================================
/// Allocates memory for float3 on GPU.
//==============================================================================
size_t Malloc(float3 **ptr,unsigned count){
  size_t size=sizeof(float3)*count;  cudaMalloc((void**)ptr,size);  return(size);
}

//==============================================================================
/// Allocates memory for float4 on GPU.
//==============================================================================
size_t Malloc(float4 **ptr,unsigned count){
  size_t size=sizeof(float4)*count;  cudaMalloc((void**)ptr,size);  return(size);
}

//==============================================================================
/// Allocates memory for double on GPU.
//==============================================================================
size_t Malloc(double **ptr,unsigned count){
  size_t size=sizeof(double)*count;  cudaMalloc((void**)ptr,size);  return(size);
}

//==============================================================================
/// Allocates memory for double2 on GPU.
//==============================================================================
size_t Malloc(double2 **ptr,unsigned count){
  size_t size=sizeof(double2)*count;  cudaMalloc((void**)ptr,size);  return(size);
}

//==============================================================================
/// Allocates memory for double3 on GPU.
//==============================================================================
size_t Malloc(double3 **ptr,unsigned count){
  size_t size=sizeof(double3)*count;  cudaMalloc((void**)ptr,size);  return(size);
}


//##############################################################################
//## Functions to allocate pinned CPU memory.
//##############################################################################

//==============================================================================
/// Allocates pinned memory for word on CPU.
//==============================================================================
size_t HostAlloc(word **ptr,unsigned count){
  size_t size=sizeof(word)*count;  cudaHostAlloc((void**)ptr,size,cudaHostAllocDefault);  return(size);
}

//==============================================================================
/// Allocates pinned memory for unsigned on CPU.
//==============================================================================
size_t HostAlloc(unsigned **ptr,unsigned count){
  size_t size=sizeof(unsigned)*count;  cudaHostAlloc((void**)ptr,size,cudaHostAllocDefault);  return(size);
}

//==============================================================================
/// Allocates pinned memory for int on CPU.
//==============================================================================
size_t HostAlloc(int **ptr,unsigned count){
  size_t size=sizeof(int)*count;  cudaHostAlloc((void**)ptr,size,cudaHostAllocDefault);  return(size);
}

//==============================================================================
/// Allocates pinned memory for int2 on CPU.
//==============================================================================
size_t HostAlloc(int2 **ptr,unsigned count){
  size_t size=sizeof(int2)*count;  cudaHostAlloc((void**)ptr,size,cudaHostAllocDefault);  return(size);
}

//==============================================================================
/// Allocates pinned memory for float on CPU.
//==============================================================================
size_t HostAlloc(float **ptr,unsigned count){
  size_t size=sizeof(float)*count;  cudaHostAlloc((void**)ptr,size,cudaHostAllocDefault);  return(size);
}

//==============================================================================
/// Allocates pinned memory for tfloat4 on CPU.
//==============================================================================
size_t HostAlloc(tfloat4 **ptr,unsigned count){
  size_t size=sizeof(tfloat4)*count;  cudaHostAlloc((void**)ptr,size,cudaHostAllocDefault);  return(size);
}

//==============================================================================
/// Allocates pinned memory for double on CPU.
//==============================================================================
size_t HostAlloc(double **ptr,unsigned count){
  size_t size=sizeof(double)*count;  cudaHostAlloc((void**)ptr,size,cudaHostAllocDefault);  return(size);
}

//==============================================================================
/// Allocates pinned memory for tdouble2 on CPU.
//==============================================================================
size_t HostAlloc(tdouble2 **ptr,unsigned count){
  size_t size=sizeof(tdouble2)*count;  cudaHostAlloc((void**)ptr,size,cudaHostAllocDefault);  return(size);
}


//##############################################################################
//## Functions to copy data to Host (debug).
//##############################################################################

//==============================================================================
/// Returns dynamic pointer with word data. (this pointer must be deleted)
//==============================================================================
word* ToHostWord(unsigned pini,unsigned n,const word *vg){
  std::string met="ToHostWord: ";
  CheckCudaErrors(met+"At the beginning.");
  try{
    word *v=new word[n];
    cudaMemcpy(v,vg+pini,sizeof(word)*n,cudaMemcpyDeviceToHost);
    CheckCudaErrors(met+"After cudaMemcpy().");
    return(v);
  }
  catch(const std::bad_alloc){
    const std::string tx=met+fun::PrintStr("Could not allocate the requested memory (np=%u).",n);
    throw tx;
  }
  return(NULL);
}

//==============================================================================
/// Returns dynamic pointer with ushort4 data. (this pointer must be deleted)
//==============================================================================
ushort4* ToHostWord4(unsigned pini,unsigned n,const ushort4 *vg){
  std::string met="ToHostWord4: ";
  CheckCudaErrors(met+"At the beginning.");
  try{
    ushort4 *v=new ushort4[n];
    cudaMemcpy(v,vg+pini,sizeof(ushort4)*n,cudaMemcpyDeviceToHost);
    CheckCudaErrors(met+"After cudaMemcpy().");
    return(v);
  }
  catch(const std::bad_alloc){
    const std::string tx=met+fun::PrintStr("Could not allocate the requested memory (np=%u).",n);
    throw tx;
  }
  return(NULL);
}

//==============================================================================
/// Returns dynamic pointer with int data. (this pointer must be deleted)
//==============================================================================
int* ToHostInt(unsigned pini,unsigned n,const int *vg){
  std::string met="ToHostInt: ";
  CheckCudaErrors(met+"At the beginning.");
  try{
    int *v=new int[n];
    cudaMemcpy(v,vg+pini,sizeof(int)*n,cudaMemcpyDeviceToHost);
    CheckCudaErrors(met+"After cudaMemcpy().");
    return(v);
  }
  catch(const std::bad_alloc){
    const std::string tx=met+fun::PrintStr("Could not allocate the requested memory (np=%u).",n);
    throw tx;
  }
  return(NULL);
}

//==============================================================================
/// Returns dynamic pointer with unsigned data. (this pointer must be deleted)
//==============================================================================
unsigned* ToHostUint(unsigned pini,unsigned n,const unsigned *vg){
  std::string met="ToHostUint: ";
  CheckCudaErrors(met+"At the beginning.");
  try{
    unsigned *v=new unsigned[n];
    cudaMemcpy(v,vg+pini,sizeof(unsigned)*n,cudaMemcpyDeviceToHost);
    CheckCudaErrors(met+"After cudaMemcpy().");
    return(v);
  }
  catch(const std::bad_alloc){
    const std::string tx=met+fun::PrintStr("Could not allocate the requested memory (np=%u).",n);
    throw tx;
  }
  return(NULL);
}

//==============================================================================
/// Returns dynamic pointer with tint2 data. (this pointer must be deleted)
//==============================================================================
tint2* ToHostInt2(unsigned pini,unsigned n,const int2 *vg){
  std::string met="ToHostInt2: ";
  CheckCudaErrors(met+"At the beginning.");
  try{
    tint2 *v=new tint2[n];
    cudaMemcpy(v,vg+pini,sizeof(tint2)*n,cudaMemcpyDeviceToHost);
    CheckCudaErrors(met+"After cudaMemcpy().");
    return(v);
  }
  catch(const std::bad_alloc){
    const std::string tx=met+fun::PrintStr("Could not allocate the requested memory (np=%u).",n);
    throw tx;
  }
  return(NULL);
}

//==============================================================================
/// Returns dynamic pointer with float data. (this pointer must be deleted)
//==============================================================================
float* ToHostFloat(unsigned pini,unsigned n,const float *vg){
  std::string met="ToHostFloat: ";
  CheckCudaErrors(met+"At the beginning.");
  try{
    float *v=new float[n];
    cudaMemcpy(v,vg+pini,sizeof(float)*n,cudaMemcpyDeviceToHost);
    CheckCudaErrors(met+"After cudaMemcpy().");
    return(v);
  }
  catch(const std::bad_alloc){
    const std::string tx=met+fun::PrintStr("Could not allocate the requested memory (np=%u).",n);
    throw tx;
  }
  return(NULL);
}

//==============================================================================
/// Returns dynamic pointer with tfloat3 data. (this pointer must be deleted)
//==============================================================================
tfloat3* ToHostFloat3(unsigned pini,unsigned n,const float3 *vg){
  std::string met="ToHostFloat3: ";
  CheckCudaErrors(met+"At the beginning.");
  try{
    tfloat3 *v=new tfloat3[n];
    cudaMemcpy(v,vg+pini,sizeof(tfloat3)*n,cudaMemcpyDeviceToHost);
    CheckCudaErrors(met+"After cudaMemcpy().");
    return(v);
  }
  catch(const std::bad_alloc){
    const std::string tx=met+fun::PrintStr("Could not allocate the requested memory (np=%u).",n);
    throw tx;
  }
  return(NULL);
}

//==============================================================================
/// Returns dynamic pointer with tfloat4 data. (this pointer must be deleted)
//==============================================================================
tfloat4* ToHostFloat4(unsigned pini,unsigned n,const float4 *vg){
  std::string met="ToHostFloat4: ";
  CheckCudaErrors(met+"At the beginning.");
  try{
    tfloat4 *v=new tfloat4[n];
    cudaMemcpy(v,vg+pini,sizeof(tfloat4)*n,cudaMemcpyDeviceToHost);
    CheckCudaErrors(met+"After cudaMemcpy().");
    return(v);
  }
  catch(const std::bad_alloc){
    const std::string tx=met+fun::PrintStr("Could not allocate the requested memory (np=%u).",n);
    throw tx;
  }
  return(NULL);
}

//==============================================================================
/// Returns dynamic pointer with double data. (this pointer must be deleted)
//==============================================================================
double* ToHostDouble(unsigned pini,unsigned n,const double *vg){
  std::string met="ToHostDouble: ";
  CheckCudaErrors(met+"At the beginning.");
  try{
    double *v=new double[n];
    cudaMemcpy(v,vg+pini,sizeof(double)*n,cudaMemcpyDeviceToHost);
    CheckCudaErrors(met+"After cudaMemcpy().");
    return(v);
  }
  catch(const std::bad_alloc){
    const std::string tx=met+fun::PrintStr("Could not allocate the requested memory (np=%u).",n);
    throw tx;
  }
  return(NULL);
}

//==============================================================================
/// Returns dynamic pointer with tdouble2 data. (this pointer must be deleted)
//==============================================================================
tdouble2* ToHostDouble2(unsigned pini,unsigned n,const double2 *vg){
  std::string met="ToHostDouble2: ";
  CheckCudaErrors(met+"At the beginning.");
  try{
    tdouble2 *v=new tdouble2[n];
    cudaMemcpy(v,vg+pini,sizeof(tdouble2)*n,cudaMemcpyDeviceToHost);
    CheckCudaErrors(met+"After cudaMemcpy().");
    return(v);
  }
  catch(const std::bad_alloc){
    const std::string tx=met+fun::PrintStr("Could not allocate the requested memory (np=%u).",n);
    throw tx;
  }
  return(NULL);
}



//==============================================================================
/// Returns dynamic pointer with position in tfloat3. (this pointer must be deleted)
//==============================================================================
tfloat3* ToHostPosf3(unsigned pini,unsigned n,const double2 *posxyg,const double *poszg){
  std::string met="ToHostPosf3: ";
  CheckCudaErrors(met+"At the beginning.");
  try{
    tdouble2 *posxy=ToHostDouble2(pini,n,posxyg);
    double   *posz= ToHostDouble(pini,n,poszg);
    tfloat3 *posf=new tfloat3[n];
    for(unsigned p=0;p<n;p++)posf[p]=ToTFloat3(TDouble3(posxy[p].x,posxy[p].y,posz[p]));
    delete[] posxy;  posxy=NULL;
    delete[] posz;   posz =NULL;
    return(posf);
  }
  catch(const std::bad_alloc){
    const std::string tx=met+fun::PrintStr("Could not allocate the requested memory (np=%u).",n);
    throw tx;
  }
  return(NULL);
}

//==============================================================================
/// Returns dynamic pointer with position in tfloat3. (this pointer must be deleted)
//==============================================================================
tdouble3* ToHostPosd3(unsigned pini,unsigned n,const double2 *posxyg,const double *poszg){
  std::string met="ToHostPosd3: ";
  CheckCudaErrors(met+"At the beginning.");
  try{
    tdouble2 *posxy=ToHostDouble2(pini,n,posxyg);
    double   *posz= ToHostDouble(pini,n,poszg);
    tdouble3 *posd=new tdouble3[n];
    for(unsigned p=0;p<n;p++)posd[p]=TDouble3(posxy[p].x,posxy[p].y,posz[p]);
    delete[] posxy;  posxy=NULL;
    delete[] posz;   posz =NULL;
    return(posd);
  }
  catch(const std::bad_alloc){
    const std::string tx=met+fun::PrintStr("Could not allocate the requested memory (np=%u).",n);
    throw tx;
  }
  return(NULL);
}


}


