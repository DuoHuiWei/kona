#include <cstdint>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../Machines/kona-dcf-compare.hpp"
#include "../../Networking/Player.h"
#include "../../Networking/Server.h"
#include "../../Tools/ezOptionParser.h"
#include "../sec-fre.hpp"

using namespace std;
namespace
{
int playerno=0; ez::ezOptionParser opt;
void parse_argv(int argc,const char** argv){
    opt.add("5600",0,1,0,"Port base","-pn","--portnumbase");
    opt.add("",0,1,0,"Player","-p","--player");
    opt.add("",0,1,0,"Port","-mp","--my-port");
    opt.add("localhost",0,1,0,"Host","-h","--hostname");
    opt.add("",0,1,0,"IP file","-ip","--ip-file-name");
    opt.parse(argc,argv);
    if(opt.isSet("-p"))opt.get("-p")->getInt(playerno);
    else if(argc>1)std::sscanf(argv[1],"%d",&playerno);
    else throw runtime_error("usage");
}
RealTwoPartyPlayer* network(){
    string h,ip;int b=0,p=Names::DEFAULT_PORT;
    opt.get("--portnumbase")->getInt(b);opt.get("--hostname")->getString(h);
    opt.get("--ip-file-name")->getString(ip);
    if(opt.get("--my-port")->isSet)opt.get("--my-port")->getInt(p);
    Names n;if(!ip.empty())n.init(playerno,b,ip,2);
    else Server::start_networking(n,playerno,2,h,b,p);
    return new RealTwoPartyPlayer(n,1-playerno,0);
}
ConstantPP::Ring ring(uint64_t x){return ConstantPP::Ring((mp_limb_t)x);}
vector<ConstantPP::Ring> share(const vector<uint64_t>& v,int p){
    vector<ConstantPP::Ring> o(v.size());
    for(size_t i=0;i<v.size();++i){auto s=ring(1200+17*i);o[i]=p==0?s:ring(v[i])-s;}
    return o;
}
vector<uint64_t> reveal(RealTwoPartyPlayer* P,int p,const vector<ConstantPP::Ring>& a){
    if(p==0){octetStream os;P->receive(os);vector<uint64_t> o(a.size());
        for(size_t i=0;i<a.size();++i){ConstantPP::Ring q;q.unpack(os);o[i]=(a[i]+q).get_limb(0);}return o;}
    octetStream os;for(auto&x:a)x.pack(os);P->send(os);return {};
}
}
int main(int argc,const char** argv){
    parse_argv(argc,argv);auto*P=network();
    const vector<uint64_t> labels={1,2,1,3,1};
    auto s=share(labels,playerno);
    KonaDcfCompare::Compare64<64> dcf(P,playerno);
    ConstantPP::ProtocolStats ps;ConstantPP::SecFreStats fs;
    auto f=ConstantPP::sec_fre_kona_dcf(P,playerno,s,dcf,&ps,&fs);
    auto open=reveal(P,playerno,f);
    bool ok=true;if(playerno==0){
        const vector<uint64_t> expected={3,1,3,1,3};
        ok=open==expected&&ps.logical_rounds==1&&fs.upper_triangle_eq_predicates==10;
        cout<<"test=constantpp_sec_fre\nfrequency=";
        for(size_t i=0;i<open.size();++i){if(i)cout<<",";cout<<open[i];}
        cout<<"\nlogical_rounds="<<ps.logical_rounds
            <<"\nupper_triangle_eq_predicates="<<fs.upper_triangle_eq_predicates
            <<"\nkona_secure_compare_pairs="<<fs.eq.kona_secure_compare_pairs
            <<"\ndcf_evaluate_calls="<<fs.eq.kona_dcf_evaluate_calls
            <<"\nresult="<<(ok?"PASS":"FAIL")<<endl;
    }
    delete P;return ok?0:1;
}
