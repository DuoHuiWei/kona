#ifndef CONSTANTPP_SEC_BCOM_MATERIAL_HPP_
#define CONSTANTPP_SEC_BCOM_MATERIAL_HPP_

#include <cstddef>
#include <vector>

#include "constantpp-types.hpp"

namespace ConstantPP
{

/*
 * One unordered pair in the source-like upper-triangular SecBCom plan.
 * Only i < j is stored.
 */
struct SecBComPair
{
    std::size_t i = 0;
    std::size_t j = 0;
};

/*
 * Engineering plan matching the public constantPP-KNN source:
 * compare only the upper triangle i < j, then update both rank counters
 * from the same secret comparison result.
 *
 * Number of comparisons:
 *     n(n-1)/2
 */
struct SecBComUpperTrianglePlan
{
    std::size_t n = 0;
    std::vector<SecBComPair> pairs;
};

SecBComUpperTrianglePlan make_sec_bcom_upper_triangle_plan(
        std::size_t n);

} // namespace ConstantPP

#endif // CONSTANTPP_SEC_BCOM_MATERIAL_HPP_
