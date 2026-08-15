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
#include "../sec-eq.hpp"

using namespace std;

namespace
{
int playerno = 0;
ez::ezOptionParser opt;

void parse_argv(int argc, const char** argv)
{
    opt.add("5500",0,1,0,"Port base","-pn","--portnumbase");
    opt.add("",0,1,0,"Player","-p","--player");
    opt.add("",0,1,0,"Port","-mp","--my-port");
    opt.add("localhost",0,1,0,"Host","-h","--hostname");
    opt.add("",0,1,0,"IP file","-ip","--ip-file-name");
    opt.parse(argc, argv);
    if (opt.isSet("-p")) opt.get("-p")->getInt(playerno);
    else if (argc > 1) std::sscanf(argv[1], "%d", &playerno);
    else throw runtime_error("usage: test-sec-eq.x -p 0|1");
}

RealTwoPartyPlayer* network()
{
    string host, ip; int base=0, port=Names::DEFAULT_PORT;
    opt.get("--portnumbase")->getInt(base);
    opt.get("--hostname")->getString(host);
    opt.get("--ip-file-name")->getString(ip);
    if (opt.get("--my-port")->isSet) opt.get("--my-port")->getInt(port);
    Names names;
    if (!ip.empty()) names.init(playerno, base, ip, 2);
    else Server::start_networking(names, playerno, 2, host, base, port);
    return new RealTwoPartyPlayer(names, 1-playerno, 0);
}

ConstantPP::Ring ring(uint64_t x)
{ return ConstantPP::Ring(static_cast<mp_limb_t>(x)); }

vector<ConstantPP::Ring> share(const vector<uint64_t>& v, int p)
{
    vector<ConstantPP::Ring> out(v.size());
    for (size_t i=0;i<v.size();++i) {
        auto s0=ring(900+29*i);
        out[i]=p==0?s0:ring(v[i])-s0;
    }
    return out;
}

vector<uint64_t> reveal(RealTwoPartyPlayer* P,int p,
                        const vector<ConstantPP::Ring>& local)
{
    if (p==0) {
        octetStream os; P->receive(os);
        vector<uint64_t> out(local.size());
        for (size_t i=0;i<local.size();++i) {
            ConstantPP::Ring q; q.unpack(os);
            out[i]=(local[i]+q).get_limb(0);
        }
        return out;
    }
    octetStream os; for (auto& x:local) x.pack(os); P->send(os); return {};
}
}

int main(int argc,const char** argv)
{
    parse_argv(argc,argv);
    auto* P=network();

    const vector<uint64_t> plain={7,7,2,9};
    auto x=share(plain,playerno);
    auto plan=ConstantPP::make_sec_bcom_upper_triangle_plan(plain.size());
    KonaDcfCompare::Compare64<64> dcf(P,playerno);

    ConstantPP::ProtocolStats ps;
    ConstantPP::SecEqStats es;
    auto eq=ConstantPP::sec_eq_pairs_kona_dcf(
            P,playerno,x,plan,dcf,&ps,&es);
    auto opened=reveal(P,playerno,eq);

    bool ok=true;
    if (playerno==0) {
        const vector<uint64_t> expected={1,0,0,0,0,0};
        ok=opened==expected && ps.logical_rounds==1
           && es.equality_predicates==6
           && es.kona_secure_compare_pairs==12;
        cout<<"test=constantpp_sec_eq\n";
        cout<<"eq_upper_triangle=";
        for(size_t i=0;i<opened.size();++i){if(i)cout<<",";cout<<opened[i];}
        cout<<"\nlogical_rounds="<<ps.logical_rounds
            <<"\nequality_predicates="<<es.equality_predicates
            <<"\nkona_secure_compare_pairs="<<es.kona_secure_compare_pairs
            <<"\ndcf_evaluate_calls="<<es.kona_dcf_evaluate_calls
            <<"\nresult="<<(ok?"PASS":"FAIL")<<endl;
    }
    delete P; return ok?0:1;
}
