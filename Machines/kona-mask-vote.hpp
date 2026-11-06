#ifndef KONA_MASK_VOTE_HPP
#define KONA_MASK_VOTE_HPP
#include "kona-pcr-compare.hpp"
#include "kona-dcf-compare.hpp"
#include <limits>
#include <numeric>
#include "kona-share-conversion.hpp"
#include "kona-cong-kona-adapter.hpp"

namespace KonaMaskVote {
template<int K> using SharePair = std::array<Z2<K>, 2>;
template<int K>
std::vector<Z2<K>> public_constants(size_t n, uint64_t value, RealTwoPartyPlayer* player)
{
    std::vector<Z2<K>> values(n, Z2<K>(static_cast<mp_limb_t>(value)));
    return KonaShareConversion::public_to_additive_batch_l2<K>(values, player);
}
// Counts retain the existing public-domain, masked equality semantics.
// Flatten ALL categories and both strict predicates into ONE compare batch;
// then ONE masked-multiplication batch. Internal chunk ceilings still apply.
// No labels, masks or counts are opened. Auxiliary material remains existing L2.
template<int K, class CompareLess>
void label_counts_batched(const std::vector<SharePair<K>>& shares,
        const std::vector<Z2<K>>& mask, int num_label,
        std::vector<SharePair<K>>& counts, RealTwoPartyPlayer* player,
        CompareLess less)
{
    if (num_label <= 0 || shares.size() != mask.size())
        throw std::invalid_argument("mask vote: invalid domain or mask size");
    const size_t n=shares.size();
    if (n && size_t(num_label)>size_t(std::numeric_limits<int>::max())/4/n)
        throw std::length_error("mask vote: flattened comparison indices overflow");
    const size_t total=n*size_t(num_label);
    counts.assign(num_label,SharePair<K>{Z2<K>(0),Z2<K>(0)});
    if (!total) {
        for(int c=0;c<num_label;++c) counts[c][1]=public_constants<K>(1,c,player)[0];
        return;
    }
    std::vector<Z2<K>> left(2*total),right(2*total),expanded_mask(total);
    for(int c=0;c<num_label;++c) {
        const auto label=KonaShareConversion::public_to_additive_l2<K>(Z2<K>(c),player);
        for(size_t i=0;i<n;++i) {
            const size_t j=size_t(c)*n+i;
            left[j]=shares[i][1];right[j]=label;
            left[total+j]=label;right[total+j]=shares[i][1];
            expanded_mask[j]=mask[i];
        }
        counts[c][1]=label;
    }
    const auto bits=less(left,right);
    const auto one=public_constants<K>(1,1,player)[0];
    std::vector<Z2<K>> eq(total),weighted;
    for(size_t j=0;j<total;++j) eq[j]=one-bits[j]-bits[total+j];
    KonaShareConversion::mul_vector_additive_kona_l2_chunked<K>(expanded_mask,eq,weighted,player);
    for(int c=0;c<num_label;++c)
        for(size_t i=0;i<n;++i) counts[c][0]+=weighted[size_t(c)*n+i];
}

template<int K>
void label_counts_with_pcr(const std::vector<SharePair<K>>& shares,
        const std::vector<Z2<K>>& mask,int num_label,
        std::vector<SharePair<K>>& counts,RealTwoPartyPlayer* player)
{
    label_counts_batched<K>(shares,mask,num_label,counts,player,[&](const auto& x,const auto& y) {
        return KonaPcrCompare::pcr_compare_lt_batch_l2(x,y,player);
    });
}

template<int K>
void label_counts_with_dcf(const std::vector<SharePair<K>>& shares,
        const std::vector<Z2<K>>& mask,int num_label,
        std::vector<SharePair<K>>& counts,RealTwoPartyPlayer* player,
        KonaDcfCompare::Compare64<K>& dcf)
{
    label_counts_batched<K>(shares,mask,num_label,counts,player,[&](const auto& x,const auto& y) {
        std::vector<Z2<K>> values(2*x.size()),bits(2*x.size()),out(x.size());
        std::vector<int> idx(values.size());std::iota(idx.begin(),idx.end(),0);
        for(size_t i=0;i<x.size();++i){values[2*i]=x[i];values[2*i+1]=y[i];}
        dcf.compare_in_vec(values,idx,bits,false);
        for(size_t i=0;i<x.size();++i)out[i]=bits[2*i];
        return out;
    });
}

// New final selection over the existing secret category counts. Strict '<'
// swaps put the larger count on the left; ties retain the smaller public
// category index. Comparisons and swaps are real existing PCR/L2 operations.
template<int K, class ComparePairs>
Z2<K> winner_from_counts(std::vector<SharePair<K>> counts,
        RealTwoPartyPlayer* player, ComparePairs compare)
{
    if (counts.empty()) throw std::invalid_argument("vote requires a label domain");
    std::vector<int> active;
    for (size_t i = 0; i < counts.size(); ++i) active.push_back(int(i));
    while (active.size() > 1)
    {
        std::vector<int> idx, next;
        for (size_t i = 0; i + 1 < active.size(); i += 2)
        {
            idx.push_back(active[i]); idx.push_back(active[i + 1]);
            next.push_back(active[i]);
        }
        if (active.size() % 2) next.push_back(active.back());
        std::vector<Z2<K>> bits(idx.size());
        compare(counts,idx,bits);
        KonaCongKonaAdapter::ss_vec_kona_l2<K>(counts, idx, bits, player);
        active.swap(next);
    }
    return counts[active[0]][1];
}

template<int K>
Z2<K> winner_from_counts_with_pcr(std::vector<SharePair<K>> counts,RealTwoPartyPlayer* player)
{
    return winner_from_counts<K>(std::move(counts),player,[&](const auto& rows,const auto& idx,auto& bits){
        KonaPcrCompare::pcr_compare_in_vec_l2(rows,idx,bits,false,player);
    });
}

template<int K>
Z2<K> winner_from_counts_with_dcf(std::vector<SharePair<K>> counts,RealTwoPartyPlayer* player,
        KonaDcfCompare::Compare64<K>& dcf)
{
    return winner_from_counts<K>(std::move(counts),player,[&](const auto& rows,const auto& idx,auto& bits){
        dcf.compare_in_vec(rows,idx,bits,false);
    });
}

template<int K>
Z2<K> vote_with_dcf(const std::vector<SharePair<K>>& candidates,
        const std::vector<Z2<K>>& mask,int num_labels,RealTwoPartyPlayer* player,
        KonaDcfCompare::Compare64<K>& dcf)
{
    std::vector<SharePair<K>> counts;
    label_counts_with_dcf<K>(candidates,mask,num_labels,counts,player,dcf);
    return winner_from_counts_with_dcf<K>(std::move(counts),player,dcf);
}

template<int K>
Z2<K> vote_with_pcr(const std::vector<SharePair<K>>& candidates,
        const std::vector<Z2<K>>& mask, int num_labels, RealTwoPartyPlayer* player)
{
    std::vector<SharePair<K>> counts;
    label_counts_with_pcr<K>(candidates, mask, num_labels, counts, player);
    return winner_from_counts_with_pcr<K>(std::move(counts), player);
}
// Optimized DCF path for the pruned/block-matrix experiment. Baseline PCR and
// DCF interfaces above stay available, including Shao's existing baseline vote.
// Preconditions: labels are integers in [0,C); mask bits select the candidates.
// PublicAllSelected is a caller declaration, NEVER inferred from secret shares.
struct DcfVoteStats {
    uint64_t count_predicates=0,winner_predicates=0,mul_items=0;
    uint64_t compare_chunks=0,mul_chunks=0;
    uint64_t compare_batches=0,mul_batches=0;
    uint64_t logical_rounds() const {return compare_batches+mul_batches;}
    uint64_t exchanges() const {return compare_chunks+mul_chunks;}
    bool public_all_selected=false,direct_label=false,single_selected=false;
    uint64_t comparators() const {return count_predicates+winner_predicates;}
};

template<int K>
void label_counts_dcf_optimized(const std::vector<SharePair<K>>& candidates,
        const std::vector<Z2<K>>& mask,int classes,RealTwoPartyPlayer* player,
        KonaDcfCompare::Compare64<K>& dcf,std::vector<SharePair<K>>& counts,
        bool public_all_selected=false,DcfVoteStats* stats=nullptr)
{
    const size_t n=candidates.size();
    if(classes<1||(!public_all_selected&&mask.size()!=n)||
            (public_all_selected&&!mask.empty()))
        throw std::invalid_argument("optimized vote: domain/mask declaration mismatch");
    if(size_t(classes-1)&&n>size_t(std::numeric_limits<int>::max())/2/size_t(classes-1))
        throw std::length_error("optimized vote: compare indices overflow");
    counts.assign(classes,SharePair<K>{Z2<K>(0),Z2<K>(0)});
    for(int c=0;c<classes;++c)
        counts[c][1]=KonaShareConversion::public_to_additive_l2<K>(Z2<K>(c),player);
    Z2<K> total(0);
    if(public_all_selected)
        total=KonaShareConversion::public_to_additive_l2<K>(Z2<K>(static_cast<mp_limb_t>(n)),player);
    else for(const auto& bit:mask)total+=bit;
    if(classes==1){counts[0][0]=total;return;}
    const size_t predicates=n*size_t(classes-1);
    if(!predicates)return;
    std::vector<Z2<K>> values(2*predicates),bits(2*predicates);
    std::vector<int> idx(values.size());std::iota(idx.begin(),idx.end(),0);
    for(int boundary=1;boundary<classes;++boundary){
        const auto threshold=KonaShareConversion::public_to_additive_l2<K>(Z2<K>(boundary),player);
        for(size_t i=0;i<n;++i){
            const size_t j=size_t(boundary-1)*n+i;
            values[2*j]=candidates[i][1];values[2*j+1]=threshold;
        }
    }
    dcf.compare_in_vec(values,idx,bits,false,KonaBenchConfig::DCF_CHUNK);
    if(stats){stats->count_predicates+=predicates;++stats->compare_batches;stats->compare_chunks+=KonaBenchConfig::chunks(predicates,KonaBenchConfig::DCF_CHUNK);}
    std::vector<Z2<K>> weighted(predicates);
    for(size_t j=0;j<predicates;++j)weighted[j]=bits[2*j];
    if(!public_all_selected){
        std::vector<Z2<K>> expanded_mask(predicates),products;
        for(size_t j=0;j<predicates;++j)expanded_mask[j]=mask[j%n];
        KonaShareConversion::mul_vector_additive_kona_l2_chunked<K>(weighted,expanded_mask,products,player);
        weighted.swap(products);if(stats){stats->mul_items+=predicates;++stats->mul_batches;stats->mul_chunks+=KonaBenchConfig::chunks(predicates,KonaBenchConfig::MUL_CHUNK);}
    }
    // F(c)=sum mask*[label<c]. F(0)=0; F(C)=total selected mask.
    // count(c)=F(c+1)-F(c); no opening or communication for the differences.
    Z2<K> previous(0);
    for(int boundary=1;boundary<classes;++boundary){
        Z2<K> cumulative(0);
        for(size_t i=0;i<n;++i)cumulative+=weighted[size_t(boundary-1)*n+i];
        counts[boundary-1][0]=cumulative-previous;previous=cumulative;
    }
    counts.back()[0]=total-previous;
}

template<int K>
Z2<K> winner_dcf_optimized(std::vector<SharePair<K>> counts,
        RealTwoPartyPlayer* player,KonaDcfCompare::Compare64<K>& dcf,
        DcfVoteStats* stats=nullptr)
{
    if(counts.empty())throw std::invalid_argument("optimized vote requires classes");
    std::vector<int> active(counts.size());std::iota(active.begin(),active.end(),0);
    // Precondition: initial labels are the public category indices set by
    // label_counts_dcf_optimized. This is not a general arbitrary-label argmax.
    std::vector<bool> public_label(counts.size(),true);
    while(active.size()>1){
        const size_t pairs=active.size()/2,next_size=(active.size()+1)/2;
        const bool final_layer=next_size==1;
        std::vector<int> idx,next;idx.reserve(2*pairs);next.reserve(next_size);
        for(size_t p=0;p<pairs;++p){idx.push_back(active[2*p]);idx.push_back(active[2*p+1]);next.push_back(active[2*p]);}
        if(active.size()%2)next.push_back(active.back());
        std::vector<Z2<K>> bits(idx.size());dcf.compare_in_vec(counts,idx,bits,false,KonaBenchConfig::DCF_CHUNK);
        if(stats){stats->winner_predicates+=pairs;++stats->compare_batches;stats->compare_chunks+=KonaBenchConfig::chunks(pairs,KonaBenchConfig::DCF_CHUNK);}
        const size_t absent=std::numeric_limits<size_t>::max();
        std::vector<size_t> count_slot(pairs,absent),label_slot(pairs,absent);
        std::vector<Z2<K>> differences,selectors,products;
        for(size_t p=0;p<pairs;++p){
            const int l=idx[2*p],r=idx[2*p+1];
            if(!final_layer){count_slot[p]=differences.size();differences.push_back(counts[r][0]-counts[l][0]);selectors.push_back(bits[2*p]);}
            if(!(public_label[l]&&public_label[r])){
                label_slot[p]=differences.size();differences.push_back(counts[r][1]-counts[l][1]);selectors.push_back(bits[2*p]);
            }
        }
        if(!differences.empty()){
            KonaShareConversion::mul_vector_additive_kona_l2_chunked<K>(differences,selectors,products,player);
            if(stats){stats->mul_items+=differences.size();++stats->mul_batches;stats->mul_chunks+=KonaBenchConfig::chunks(differences.size(),KonaBenchConfig::MUL_CHUNK);}
        }
        for(size_t p=0;p<pairs;++p){
            const int l=idx[2*p],r=idx[2*p+1];
            if(count_slot[p]!=absent)counts[l][0]+=products[count_slot[p]];
            if(label_slot[p]!=absent)counts[l][1]+=products[label_slot[p]];
            else counts[l][1]=KonaShareConversion::public_to_additive_l2<K>(Z2<K>(l),player)+bits[2*p]*Z2<K>(r-l);
            public_label[l]=false;
        }
        active.swap(next);
    }
    return counts[active[0]][1];
}

template<int K>
Z2<K> vote_dcf_optimized(const std::vector<SharePair<K>>& candidates,
        const std::vector<Z2<K>>& mask,int classes,RealTwoPartyPlayer* player,
        KonaDcfCompare::Compare64<K>& dcf,bool public_all_selected=false,
        DcfVoteStats* stats_out=nullptr,size_t public_selected_count=0)
{
    if(classes<1||(!public_all_selected&&mask.size()!=candidates.size())||
            (public_all_selected&&!mask.empty()))
        throw std::invalid_argument("optimized vote: invalid input declaration");
    if(public_selected_count>candidates.size()||
            (public_all_selected&&public_selected_count&&public_selected_count!=candidates.size()))
        throw std::invalid_argument("optimized vote: invalid public selected count");
    DcfVoteStats stats;stats.public_all_selected=public_all_selected;
    Z2<K> winner(0);
    if(classes==1||candidates.empty()){
        winner=KonaShareConversion::public_to_additive_l2<K>(Z2<K>(0),player);
    }else if((public_all_selected||public_selected_count==1)&&candidates.size()==1){
        winner=candidates[0][1];stats.direct_label=true;
    }else if(public_selected_count==1){
        // The caller publicly guarantees sum(mask)=1 (Top-1). No majority
        // computation is needed; weighted label sum is the winner share.
        std::vector<Z2<K>> labels(candidates.size()),products;
        for(size_t i=0;i<candidates.size();++i)labels[i]=candidates[i][1];
        KonaShareConversion::mul_vector_additive_kona_l2_chunked<K>(labels,mask,products,player);
        for(const auto& value:products)winner+=value;
        stats.single_selected=true;stats.mul_items=candidates.size();
        stats.mul_batches=1;stats.mul_chunks=KonaBenchConfig::chunks(candidates.size(),KonaBenchConfig::MUL_CHUNK);
    }else{
        std::vector<SharePair<K>> counts;
        label_counts_dcf_optimized<K>(candidates,mask,classes,player,dcf,counts,public_all_selected,&stats);
        winner=winner_dcf_optimized<K>(std::move(counts),player,dcf,&stats);
    }
    if(stats_out)*stats_out=stats;
    return winner;
}

} // namespace KonaMaskVote
#endif
