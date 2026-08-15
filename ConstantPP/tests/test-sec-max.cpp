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
#include "../sec-max.hpp"

using namespace std;
namespace
{
int playerno=0; ez::ezOptionParser opt;
void parse_argv(int argc,const char** argv){
    opt.add("5700",0,1,0,"Port base","-pn","--portnumbase");
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
vector<ConstantPP::Ring> share(const vector<uint64_t>& v,int p,uint64_t base){
    vector<ConstantPP::Ring> o(v.size());
    for(size_t i=0;i<v.size();++i){auto s=ring(base+23*i);o[i]=p==0?s:ring(v[i])-s;}
    return o;
}
ConstantPP::BeaverTripleParty triple(int p){
    // a=5,b=7,c=35; shares a=(2,3), b=(3,4), c=(11,24)
    ConstantPP::BeaverTripleParty t;
    if(p==0){t.a_share=ring(2);t.b_share=ring(3);t.c_share=ring(11);}
    else{t.a_share=ring(3);t.b_share=ring(4);t.c_share=ring(24);}
    return t;
}
uint64_t reveal(RealTwoPartyPlayer* P,int p,const ConstantPP::Ring& x){
    if(p==0){octetStream os;P->receive(os);ConstantPP::Ring q;q.unpack(os);return (x+q).get_limb(0);}
    octetStream os;x.pack(os);P->send(os);return 0;
}
}
int main(int argc,const char** argv){
    parse_argv(argc,argv);auto*P=network();
    /*
     * Maximum-frequency tie with DIFFERENT labels:
     *
     * freq  = [3,1,3,1,3]
     * label = [7,2,8,3,9]
     *
     * The source-like upper-triangle Greater rank count defines a
     * deterministic total order for ties by assigning the reverse direction
     * as 1-b. For equal values at positions 0,2,4, the later index wins
     * pairwise tie complements, so position 4 receives su=k-1 among the
     * max-frequency entries and label 9 must be selected.
     */
    const vector<uint64_t> freq={3,1,3,1,3};
    const vector<uint64_t> label={7,2,8,3,9};
    auto fs=share(freq,playerno,3000);auto ls=share(label,playerno,4000);
    KonaDcfCompare::Compare64<64> dcf(P,playerno);
    ConstantPP::ProtocolStats ps;ConstantPP::SecMaxStats ms;
    auto out=ConstantPP::sec_max_kona_dcf(
        P,playerno,fs,ls,triple(playerno),dcf,&ps,&ms);
    auto clear=reveal(P,playerno,out);
    bool ok=true;if(playerno==0){
        ok=clear==9&&ps.logical_rounds==3
           &&ms.bcom.unordered_pairs==10
           &&ms.eq.equality_predicates==5
           &&ms.secure_mul_pairs==5;
        cout<<"test=constantpp_sec_max\n"
            <<"revealed_label="<<clear
            <<"\nlogical_rounds="<<ps.logical_rounds
            <<"\nbcom_upper_triangle_pairs="<<ms.bcom.unordered_pairs
            <<"\neq_predicates="<<ms.eq.equality_predicates
            <<"\nsecure_mul_pairs="<<ms.secure_mul_pairs
            <<"\nresult="<<(ok?"PASS":"FAIL")<<endl;
    }
    delete P;return ok?0:1;
}
