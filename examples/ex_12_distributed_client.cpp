// example 12: connect a client to a running coordinator.
#include "kernelcache/kernelcache.hpp"
#include "kernelcache/distributed.hpp"
#include "dist_tcp.hpp"
#include <iostream>
#include <cstdlib>
using namespace kernelcache;
int main(int argc, char** argv){
  if (argc < 3){ std::cout<<"usage: example12 <host> <port> [op]\n"; return 2; }
  std::string host=argv[1]; std::uint16_t port=(std::uint16_t)std::atoi(argv[2]);
  std::string op= argc>3? argv[3] : "vec_add";
  KernelCompatibilityKey k; k.operation=op; k.artifact_format="kc-synth"; k.vendor=DeviceVendor::CPU;
  k.family=AcceleratorFamily::CPU; k.arch="x86_64"; k.runtime_backend="cpu-synth"; k.compiler_name="msvc";
  k.compiler_version_major=19; k.compiler_version_minor=44; k.abi_name="kc-synth-1.0"; k.abi_version=1;
  k.kernel_interface_signature="void(float*,const float*,uint64)"; k.dtypes={Datatype::F32}; k.layouts={TensorLayout::RowMajor};
  k.rank=1; k.shape={1024}; k.alignment_bytes=16; k.block_x=256; k.launch_abi="grid-block"; k.finalize();
  DistAuthority a; a.epoch.value=1; a.cache_gen.value=1;
  dist::TcpConnection conn; if(!conn.connect(host,port)){ std::cout<<"connect failed\n"; return 1; }
  auto req=encode_lookup_request(a,RequestId(1,1),op,std::vector<std::uint8_t>(k.canonical_bytes().begin(),k.canonical_bytes().end()),"",3);
  conn.write_frame((std::uint16_t)DistMsgType::LookupReq, req);
  std::uint16_t t; std::vector<std::uint8_t> pl;
  if(!conn.read_frame(t,pl)){ std::cout<<"no response\n"; return 1; }
  RequestId rr; std::uint8_t out; ArtifactId id; ArtifactGeneration gen; std::uint8_t compat;
  decode_lookup_response(pl,rr,out,id,gen,compat);
  std::cout<<"outcome=0x"<<std::hex<<(int)out<<std::dec<<" id="<<id.str().substr(0,8)<<"\n";
  return 0;
}