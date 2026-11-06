#include <iostream>
#include <stdexcept>
#include <limits>
#include "kona-reduced-depth-cong-topk.hpp"

void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
std::vector<int> plain_mask(const KonaReducedDepthCongTopK::TerminalPlan& plan,
        const std::vector<int>& values, int k) {
    auto rank=plan.initial_rank;
    if(rank.empty()) rank.assign(values.size(),0);
    for(size_t p=0;p<plan.matrix_idx.size();p+=2) {
        int i=plan.matrix_idx[p],j=plan.matrix_idx[p+1];
        int c=values[i]>values[j]; rank[i]+=c; rank[j]+=1-c;
    }
    for(auto& r:rank) r=r<k;
    return rank;
}
int main() {
    using namespace KonaReducedDepthCongTopK;
    const auto p=prepare_reduced_plan(200,5);
    require(p.plan.blocks.size()==2 && p.plan.blocks[0].real_size==128 && p.plan.blocks[1].real_size==72,"partition changed");
    const auto full=prepare_matrix({5,5},5,Terminal::Full);
    const auto pruned=prepare_matrix({5,5},5,Terminal::Pruned);
    require(p.plan.block_comparators==680 && p.plan.block_depth==22,"Cong shape changed");
    require(full.matrix_comparators==45 && full.mask_comparators==10,"full shape changed");
    require(pruned.matrix_comparators==15 && pruned.mask_comparators==10,"pruned shape changed");
    const auto single=prepare_matrix({1},1,Terminal::Pruned);
    require(single.comparators==1 && single.depth==1,"standalone single-candidate mask");
    bool rejected=false;
    try { prepare_reduced_plan(1,1); } catch (const std::runtime_error&) { rejected=true; }
    require(rejected,"frozen N=1 partition rejection changed");
    size_t checks=0;
    for(int a=0;a<=5;++a) for(int b=0;b<=3;++b) for(int c=0;c<=5;++c) {
        std::vector<int> values;
        for(int i=0;i<5;++i) values.push_back(i>=a);
        for(int i=0;i<3;++i) values.push_back(i>=b);
        for(int i=0;i<5;++i) values.push_back(i>=c);
        const auto f=prepare_matrix({5,3,5},5,Terminal::Full);
        const auto q=prepare_matrix({5,3,5},5,Terminal::Pruned);
        require(plain_mask(f,values,5)==plain_mask(q,values,5),"pruned mask differs on sorted binary blocks");
        ++checks;
    }
    const auto tail=prepare_reduced_plan(257,31);
    require(tail.plan.blocks.back().real_size==1 && tail.plan.blocks.back().keep==1,"short tail padded");
    require(!prepare_terminal(tail,Terminal::ReversePrefilter).reverse_used,"short tail must fall back");
    require(prepare_terminal(p,Terminal::ReversePrefilter).comparators==5,"two-block shortcut changed");
    const auto at=prepare_reduced_plan(199,32);
    require(at.plan.lmax==128 && !at.plan.outer_partition_cancelled &&
            at.plan.blocks.size()==2 && at.plan.blocks[0].real_size==128 &&
            at.plan.blocks[1].real_size==71,"quarter equality must still split 128+71");
    require(!tail.plan.outer_partition_cancelled,"cutoff must not use short tail length");
    for(int k : {33,100,199}) {
        const auto single=prepare_reduced_plan(199,k);
        const auto original=KonaCongTopK::build_cong_network(199,k);
        require(single.plan.lmax==128 && single.plan.outer_partition_cancelled &&
                single.plan.blocks.size()==1 && single.plan.blocks[0].real_size==199 &&
                single.plan.blocks[0].keep==k,"quarter cutoff must use one real N-sized block");
        require(single.plan.blocks[0].network.levels==original.levels &&
                single.plan.blocks[0].network.output_wires==original.output_wires,
                "fallback must preserve exact original Cong topology and output wires");
        require(single.plan.matrix.all_pairs.empty() && single.pruned_matrix_idx.empty() &&
                single.plan.matrix_comparators==0 && single.plan.total_comparators==comparator_count(original),
                "single Cong must not construct a merge matrix");
        for(auto kind : {Terminal::Full,Terminal::Pruned,Terminal::ReverseTwo,
                         Terminal::ReversePrefilter,Terminal::Obssn,Terminal::Octm}) {
            const auto terminal=prepare_terminal(single,kind);
            require(terminal.direct && !terminal.network && terminal.comparators==0 &&
                    terminal.mask_comparators==0 && terminal.input_count==size_t(k),
                    "single Cong must bypass terminal comparisons and mask");
        }
    }
    const int imax=std::numeric_limits<int>::max();
    require(should_cancel_outer_partition(imax,imax) &&
            !should_cancel_outer_partition(imax/4,imax),"4*k must not overflow int");
    std::cout << "PASS module plan and " << checks << " exhaustive sorted-block mask equivalence cases\n";
}
