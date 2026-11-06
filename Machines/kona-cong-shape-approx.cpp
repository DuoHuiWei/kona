#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "kona-cong-topk.hpp"

using namespace std;

namespace
{

struct CaseKey { int n; int k; };
struct Metrics
{
    int n=0,k=0; string method;
    int actual_depth=0,predicted_depth=0,depth_error=0;
    long long actual_total=0,predicted_total=0,total_abs_error=0;
    double total_relative_error=0, layer_mae=0, layer_rmse=0;
    int max_layer_abs_error=0;
    double max_layer_relative_error=0, mean_layer_relative_error=0, signed_layer_bias=0;
    bool exact_case=false, depth_match=false;
};
struct LayerRow
{
    int n,k,layer,actual,predicted,abs_error,signed_error; string method;
    double relative_error;
};
struct WorstCase
{
    bool set=false; double value=-1; int n=0,k=0,first_mismatch=-1,worst_layer=-1,actual_m=0,pred_m=0; string method;
    vector<int> actual,pred;
};

string vec_str(const vector<int>& v)
{
    string s="[";
    for(size_t i=0;i<v.size();++i){ if(i) s+=","; s+=to_string(v[i]); }
    s+="]"; return s;
}

vector<int> counts(const vector<KonaCongTopK::CongLevel>& levels)
{
    vector<int> out; out.reserve(levels.size());
    for (auto& l: levels) out.push_back((int)l.size());
    return out;
}
vector<int> wires(int start,int count){ vector<int> w(count); for(int i=0;i<count;++i) w[i]=start+i; return w; }
vector<int> merge_counts(int a,int k)
{
    auto r=KonaCongTopK::build_merge_network(wires(0,a), wires(a,a), k);
    return counts(r.second);
}
int ceil_log2_int(int n){ int s=0,p=1; while(p<n){p<<=1;++s;} return s; }
long long sumv(const vector<int>& v){ return accumulate(v.begin(), v.end(), 0LL); }

vector<int> predict(int n,int k,const string& method)
{
    int S=ceil_log2_int(n);
    vector<int> out;
    map<int, vector<int>> table;
    for(int s=1;s<=S;++s)
    {
        int a=min(k, 1<<(s-1));
        if(!table.count(a)) table[a]=merge_counts(a,k);
        double denom = double(1LL<<s);
        int q=1;
        if(method=="floor") q=max(1, int(floor(n/denom)));
        else if(method=="ceil") q=max(1, int(ceil(n/denom)));
        else q=max(1, int(floor(n/denom + 0.5)));
        for(int x: table[a]) out.push_back(q*x);
    }
    return out;
}

vector<int> actual_levels(int n,int k)
{
    return counts(KonaCongTopK::build_cong_network(n,k).levels);
}

vector<int> test_Ns()
{
    set<int> ns;
    for(int n=2;n<=512;++n) ns.insert(n);
    vector<int> big={600,700,800,900,1000,1023,1024,1025,1200,1500,2000,2047,2048,2049,3000,4000,4095,4096};
    ns.insert(big.begin(), big.end());
    vector<int> deltas={-15,-8,-4,-2,-1,0,1,2,4,8,15};
    for(int r=4;r<=12;++r){ int p=1<<r; for(int d:deltas) if(p+d>=2) ns.insert(p+d); }
    return vector<int>(ns.begin(), ns.end());
}

bool is_pow2(int x){ return x>0 && (x&(x-1))==0; }
int nearest_pow2_gap(int n)
{
    int lo=1; while((lo<<1)<=n) lo<<=1; int hi=lo==n?lo:lo<<1; return min(abs(n-lo), abs(n-hi));
}
string gap_bucket(int gap){ if(gap==0)return "0"; if(gap<=2)return "1..2"; if(gap<=8)return "3..8"; if(gap<=32)return "9..32"; return ">32"; }

double percentile(vector<double> v,double p){ if(v.empty()) return 0; sort(v.begin(),v.end()); double idx=(v.size()-1)*p; int lo=floor(idx),hi=ceil(idx); if(lo==hi)return v[lo]; return v[lo]*(hi-idx)+v[hi]*(idx-lo); }
double percentile_i(vector<int> v,double p){ vector<double>d(v.begin(),v.end()); return percentile(d,p); }

Metrics compute_metrics(int n,int k,const string& method,const vector<int>& a,const vector<int>& p, vector<LayerRow>& layer_rows)
{
    Metrics m; m.n=n; m.k=k; m.method=method; m.actual_depth=a.size(); m.predicted_depth=p.size(); m.depth_error=m.predicted_depth-m.actual_depth; m.depth_match=m.depth_error==0; m.actual_total=sumv(a); m.predicted_total=sumv(p); m.total_abs_error=llabs(m.predicted_total-m.actual_total); m.total_relative_error=m.actual_total?double(m.total_abs_error)/m.actual_total:0; m.exact_case=(a==p);
    int D=max(a.size(),p.size()); double se=0, abs_sum=0, rel_sum=0, signed_sum=0; int rel_count=0;
    for(int l=0;l<D;++l){ int av=l<(int)a.size()?a[l]:0; int pv=l<(int)p.size()?p[l]:0; int err=abs(pv-av); int sig=pv-av; double rel=av>0?double(err)/av:0; LayerRow row{n,k,l,av,pv,err,sig,method,rel}; layer_rows.push_back(row); abs_sum+=err; se+=double(err)*err; signed_sum+=sig; m.max_layer_abs_error=max(m.max_layer_abs_error,err); if(av>0){ m.max_layer_relative_error=max(m.max_layer_relative_error,rel); rel_sum+=rel; ++rel_count; } }
    m.layer_mae=D?abs_sum/D:0; m.layer_rmse=D?sqrt(se/D):0; m.mean_layer_relative_error=rel_count?rel_sum/rel_count:0; m.signed_layer_bias=D?signed_sum/D:0; return m;
}

int first_mismatch(const vector<int>& a,const vector<int>& p){ int D=max(a.size(),p.size()); for(int i=0;i<D;++i){ int av=i<(int)a.size()?a[i]:0; int pv=i<(int)p.size()?p[i]:0; if(av!=pv)return i;} return -1; }

struct Agg
{
    int cases=0, exact_cases=0, depth_matches=0; long long layers=0, exact_layers=0, over=0, under=0; vector<int> abs_errors; vector<double> rel_errors,total_rel; double signed_sum=0;
    void add(const Metrics&m,const vector<LayerRow>& rows,size_t start){++cases; exact_cases+=m.exact_case; depth_matches+=m.depth_match; total_rel.push_back(m.total_relative_error); for(size_t i=start;i<rows.size();++i){auto&r=rows[i];++layers; exact_layers+=r.abs_error==0; over+=r.signed_error>0; under+=r.signed_error<0; abs_errors.push_back(r.abs_error); if(r.actual>0) rel_errors.push_back(r.relative_error); signed_sum+=r.signed_error;}}
};
void print_agg(ostream& os,const string& name,const Agg&a)
{
    double mae=0,rmse=0; for(int e:a.abs_errors){mae+=e; rmse+=double(e)*e;} if(!a.abs_errors.empty()){mae/=a.abs_errors.size(); rmse=sqrt(rmse/a.abs_errors.size());}
    os<<name<<" cases="<<a.cases<<" exact_case_rate="<<(a.cases?double(a.exact_cases)/a.cases:0)<<" exact_layer_rate="<<(a.layers?double(a.exact_layers)/a.layers:0)<<" depth_match_rate="<<(a.cases?double(a.depth_matches)/a.cases:0)<<" layer_MAE="<<mae<<" layer_RMSE="<<rmse<<" abs_median="<<percentile_i(a.abs_errors,0.5)<<" abs_p90="<<percentile_i(a.abs_errors,0.9)<<" abs_p95="<<percentile_i(a.abs_errors,0.95)<<" abs_p99="<<percentile_i(a.abs_errors,0.99)<<" abs_max="<<(a.abs_errors.empty()?0:*max_element(a.abs_errors.begin(),a.abs_errors.end()))<<" rel_mean="<<(a.rel_errors.empty()?0:accumulate(a.rel_errors.begin(),a.rel_errors.end(),0.0)/a.rel_errors.size())<<" rel_median="<<percentile(a.rel_errors,0.5)<<" rel_p90="<<percentile(a.rel_errors,0.9)<<" rel_p95="<<percentile(a.rel_errors,0.95)<<" rel_p99="<<percentile(a.rel_errors,0.99)<<" total_rel_mean="<<(a.total_rel.empty()?0:accumulate(a.total_rel.begin(),a.total_rel.end(),0.0)/a.total_rel.size())<<" total_rel_median="<<percentile(a.total_rel,0.5)<<" total_rel_p95="<<percentile(a.total_rel,0.95)<<" total_rel_max="<<percentile(a.total_rel,1)<<" signed_bias="<<(a.layers? a.signed_sum/a.layers:0)<<" over_pct="<<(a.layers?double(a.over)/a.layers:0)<<" under_pct="<<(a.layers?double(a.under)/a.layers:0)<<" exact_pct="<<(a.layers?double(a.exact_layers)/a.layers:0)<<"\n";
}

} // namespace

int main()
{
    string outdir="KNN-experiment-res";
    system(("mkdir -p "+outdir).c_str());
    ofstream cases(outdir+"/cong-shape-approx-cases.csv"), layers(outdir+"/cong-shape-approx-layers.csv"), decisions(outdir+"/cong-shape-approx-decision.csv"), summary(outdir+"/cong-shape-approx-summary.txt");
    cases<<"N,k,method,actual_depth,predicted_depth,depth_error,actual_total,predicted_total,total_abs_error,total_relative_error,layer_MAE,layer_RMSE,max_layer_abs_error,max_layer_relative_error,mean_layer_relative_error,signed_layer_bias,exact_case,depth_match,N_is_power_of_two,gap_bucket,k_is_power_of_two\n";
    layers<<"N,k,method,layer,actual_m,predicted_m,abs_error,relative_error,signed_error\n";
    decisions<<"method,M_star,epsilon_label,epsilon_value,direct_decision_layers,fallback_layers,fallback_rate,decision_agreement_rate,false_PCR,false_DCF\n";
    vector<int> ns=test_Ns(); vector<string> methods={"floor","round","ceil"}; vector<int> thresholds={1,2,4,8,16,32,64,128,256,512,1024};
    map<string,Agg> global; map<string,Agg> powgrp; map<string,Agg> gapgrp; map<string,Agg> kgrp; map<string,Agg> kpowgrp;
    map<tuple<string,int,string>, long long> direct, fallback, agree, falsep, falsed;
    map<tuple<string,int,string>, double> eps_value;
    WorstCase worst[3]; map<string,int> midx={{"floor",0},{"round",1},{"ceil",2}}; WorstCase round5,round10,round20;
    vector<int> expected1024={512,512,256,512,256,256,320,128,64,128,160,64,32,64,80,32,16,32,40,16,8,16,20,8,4,8,10,4,2,4,5,2,1,2}; bool sanity1024=false;
    int tested=0;
    for(int n:ns){ for(int k=2;k<=min(32,n);++k){ vector<int>a=actual_levels(n,k); if(n==1024&&k==5) sanity1024=(a==expected1024); for(string method:methods){ vector<int>p=predict(n,k,method); vector<LayerRow> rows; Metrics m=compute_metrics(n,k,method,a,p,rows); cases<<n<<","<<k<<","<<method<<","<<m.actual_depth<<","<<m.predicted_depth<<","<<m.depth_error<<","<<m.actual_total<<","<<m.predicted_total<<","<<m.total_abs_error<<","<<m.total_relative_error<<","<<m.layer_mae<<","<<m.layer_rmse<<","<<m.max_layer_abs_error<<","<<m.max_layer_relative_error<<","<<m.mean_layer_relative_error<<","<<m.signed_layer_bias<<","<<m.exact_case<<","<<m.depth_match<<","<<is_pow2(n)<<","<<gap_bucket(nearest_pow2_gap(n))<<","<<is_pow2(k)<<"\n"; size_t start=0; global[method].add(m,rows,start); powgrp[method+string(is_pow2(n)?":Npow2":":Nnonpow2")].add(m,rows,start); gapgrp[method+":"+gap_bucket(nearest_pow2_gap(n))].add(m,rows,start); kgrp[method+":k="+to_string(k)].add(m,rows,start); kpowgrp[method+string(is_pow2(k)?":kpow2":":knonpow2")].add(m,rows,start); int wi=midx[method]; if(m.max_layer_relative_error>worst[wi].value){ worst[wi].set=true; worst[wi].value=m.max_layer_relative_error; worst[wi].n=n; worst[wi].k=k; worst[wi].method=method; worst[wi].actual=a; worst[wi].pred=p; worst[wi].first_mismatch=first_mismatch(a,p); for(auto&r:rows) if(r.relative_error==m.max_layer_relative_error){ worst[wi].worst_layer=r.layer; worst[wi].actual_m=r.actual; worst[wi].pred_m=r.predicted; break; }} if(method=="round"){ auto setfirst=[&](WorstCase& w,double th){ if(!w.set && m.max_layer_relative_error>th){w.set=true;w.value=m.max_layer_relative_error;w.n=n;w.k=k;w.method=method;w.actual=a;w.pred=p;w.first_mismatch=first_mismatch(a,p); for(auto&r:rows) if(r.relative_error>th){w.worst_layer=r.layer;w.actual_m=r.actual;w.pred_m=r.predicted;break;}}}; setfirst(round5,0.05); setfirst(round10,0.10); setfirst(round20,0.20);} for(auto&r:rows){ layers<<r.n<<","<<r.k<<","<<r.method<<","<<r.layer<<","<<r.actual<<","<<r.predicted<<","<<r.abs_error<<","<<r.relative_error<<","<<r.signed_error<<"\n"; for(int M:thresholds){ auto actualPCR=r.actual>=M; auto predPCR=r.predicted>=M; vector<pair<string,double>> eps={{"0",0},{"1",1},{"2",2},{"4",4},{"8",8},{"16",16},{"32",32},{"5pct",0.05*M},{"10pct",0.10*M},{"20pct",0.20*M}}; for(auto&e:eps){auto key=make_tuple(method,M,e.first); eps_value[key]=e.second; if(abs(r.predicted-M)<=e.second){fallback[key]++; continue;} direct[key]++; if(predPCR==actualPCR) agree[key]++; else if(predPCR&&!actualPCR) falsep[key]++; else if(!predPCR&&actualPCR) falsed[key]++; } } } } ++tested; }}
    summary<<fixed<<setprecision(6); summary<<"tested_Nk_cases="<<tested<<" sanity_1024_k5="<<(sanity1024?"PASS":"FAIL")<<"\n\nGLOBAL\n"; for(auto&m:methods) print_agg(summary,m,global[m]); summary<<"\nN_GROUPS\n"; for(auto&kv:powgrp) print_agg(summary,kv.first,kv.second); summary<<"\nGAP_GROUPS\n"; for(auto&kv:gapgrp) print_agg(summary,kv.first,kv.second); summary<<"\nK_GROUPS\n"; for(auto&kv:kgrp) print_agg(summary,kv.first,kv.second); summary<<"\nK_POWER_GROUPS\n"; for(auto&kv:kpowgrp) print_agg(summary,kv.first,kv.second);
    summary<<"\nWORST_CASES\n"; for(auto&w:worst) summary<<w.method<<" max_relative="<<w.value<<" N="<<w.n<<" k="<<w.k<<" first_mismatch="<<w.first_mismatch<<" worst_layer="<<w.worst_layer<<" actual_m="<<w.actual_m<<" predicted_m="<<w.pred_m<<" actual="<<vec_str(w.actual)<<" predicted="<<vec_str(w.pred)<<"\n"; for(auto&pr:{pair<string,WorstCase>("round_gt_5pct",round5),pair<string,WorstCase>("round_gt_10pct",round10),pair<string,WorstCase>("round_gt_20pct",round20)}) summary<<pr.first<<" N="<<pr.second.n<<" k="<<pr.second.k<<" value="<<pr.second.value<<" first_mismatch="<<pr.second.first_mismatch<<" worst_layer="<<pr.second.worst_layer<<" actual_m="<<pr.second.actual_m<<" predicted_m="<<pr.second.pred_m<<" actual="<<vec_str(pr.second.actual)<<" predicted="<<vec_str(pr.second.pred)<<"\n";
    summary<<"\nN1024_K5_PREDICTIONS\n"; for(auto&m:methods) summary<<m<<" "<<vec_str(predict(1024,5,m))<<"\n";
    for(auto&method:methods){ for(int M:thresholds){ vector<string> labs={"0","1","2","4","8","16","32","5pct","10pct","20pct"}; for(auto&lab:labs){auto key=make_tuple(method,M,lab); long long d=direct[key], f=fallback[key], total=d+f; decisions<<method<<","<<M<<","<<lab<<","<<eps_value[key]<<","<<d<<","<<f<<","<<(total?double(f)/total:0)<<","<<(d?double(agree[key])/d:0)<<","<<falsep[key]<<","<<falsed[key]<<"\n"; } } }
    cout<<"wrote "<<outdir<<"/cong-shape-approx-*.csv and summary\n";
    return 0;
}
