#include "sec-bcom-material.hpp"

namespace ConstantPP
{

SecBComUpperTrianglePlan make_sec_bcom_upper_triangle_plan(
        std::size_t n)
{
    SecBComUpperTrianglePlan plan;
    plan.n = n;
    plan.pairs.reserve(n > 0 ? n * (n - 1) / 2 : 0);

    for (std::size_t i = 0; i < n; ++i)
    {
        for (std::size_t j = i + 1; j < n; ++j)
        {
            SecBComPair pair;
            pair.i = i;
            pair.j = j;
            plan.pairs.push_back(pair);
        }
    }

    return plan;
}

} // namespace ConstantPP
