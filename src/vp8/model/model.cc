#include <assert.h>
#include <fstream>
#include <iostream>

#include "model.hh"

Model ProbabilityTables::model_;
int32_t ProbabilityTables::icos_idct_linear_8192_dequantized_[8] = {0};

#ifdef ANNOTATION_ENABLED
Context *gctx = (Context*)memset(calloc(sizeof(Context),1), 0xff, sizeof(Context));
#endif
void ProbabilityTables::serialize( std::ofstream & output ) const
{
  output.write( reinterpret_cast<char*>( &model_ ), sizeof( model_ ) );
}

void ProbabilityTables::optimize()
{
  model_.forall( [&] ( Branch & x ) { x.optimize(); } );
}


bool filter(const Branch& a,
            const Branch* b) {
    if (a.true_count() == 0 && a.false_count() == 0) {
        return false;
    }
    if (b) {
        if (a.prob() + 1 == b->prob() ||
            a.prob() == b->prob() + 1 ||
            a.prob() == b->prob()) {
            return false;
        }
    } else {
        return a.true_count () > 300 && a.false_count() > 300;
    }
    return true;
}
template<class BranchArray> void print_helper(const BranchArray& ba,
                                              const BranchArray* other,
                                              const std::string &table_name,
                                              const std::vector<std::string> &names,
                                              std::vector<uint32_t> &values,
                                              ProbabilityTables::PrintabilitySpecification print_branch_bitmask) {
    values.push_back(0);
    for (size_t i = 0; i < ba.dimsize(); ++i) {
        values.back() = i;
        auto subarray = ba.at(i);
        auto otherarray = &subarray;
        otherarray= nullptr;
        print_helper(subarray, otherarray, table_name, names, values, print_branch_bitmask);
    }
    values.pop_back();
}

bool is_printable(uint64_t true_count, uint64_t false_count,
                  double true_false_ratio, double other_ratio, bool other,
                  ProbabilityTables::PrintabilitySpecification spec) {
    if (other) {
        if (true_count + false_count >= spec.min_samples) {
            double delta = true_false_ratio - other_ratio;
            if (delta < 0) delta = -delta;
            if (delta < spec.tolerance) {
                return (ProbabilityTables::CLOSE_TO_ONE_ANOTHER & spec.printability_bitmask) ? true : false;
            } else {
                return (ProbabilityTables::PRINTABLE_OK & spec.printability_bitmask) ? true : false;
            }
        } else {
            return (ProbabilityTables::PRINTABLE_INSIGNIFICANT & spec.printability_bitmask) ? true : false;
        }
    } else {
        if (true_count + false_count >= spec.min_samples) {
            double delta = true_false_ratio - .5;
            if (delta < 0) delta = -delta;
            if (delta < spec.tolerance) {
                return (ProbabilityTables::CLOSE_TO_50 & spec.printability_bitmask) ? true : false;
            } else {
                return (ProbabilityTables::PRINTABLE_OK & spec.printability_bitmask) ? true : false;
            }
        } else {
            return (ProbabilityTables::PRINTABLE_INSIGNIFICANT & spec.printability_bitmask) ? true : false;
        }
    }
}
template<> void print_helper(const Branch& ba,
                             const Branch* other,
                             const std::string&table_name,
                             const std::vector<std::string> &names,
                             std::vector<uint32_t> &values,
                             ProbabilityTables::PrintabilitySpecification print_branch_bitmask) {
    double ratio = (ba.true_count() + 1) / (double)(ba.false_count() + ba.true_count() + 2);
    (void) ratio;
    double other_ratio = ratio;
    if (other) {
        other_ratio = (other->true_count() + 1) / (double)(other->false_count() + other->true_count() + 2);
    }
    (void) other_ratio;
    if (ba.true_count() > 0 ||  ba.false_count() > 1) {
        if (is_printable(ba.true_count(), ba.false_count(), ratio, other_ratio, !!other, print_branch_bitmask))
        {
            assert(names.size() == values.size());
            std::cout <<table_name<<"::";
            for (size_t i = 0; i < names.size(); ++i) {
                std::cout << names[i]<<'['<<values[i]<<']';
            }
            std::cout << " = (" << ba.true_count() <<", "<<  (ba.false_count() - 1) << ")";
            if (other) {
                std::cout << " = (" << other->true_count() <<", "<<  (other->false_count() - 1) << "}";
            }
            std::cout << std::endl;
        }
    }
}
template<class BranchArray> void print_all(const BranchArray &ba,
                                           const BranchArray *other_ba,
                                           const std::string &table_name,
                                           const std::vector<std::string> &names,
                                           ProbabilityTables::PrintabilitySpecification spec) {
    std::vector<uint32_t> tmp;
    print_helper(ba, other_ba, table_name, names, tmp, spec);
}

const ProbabilityTables &ProbabilityTables::debug_print(const ProbabilityTables * other,
                                                        ProbabilityTables::PrintabilitySpecification spec)const
{
    print_all(model_.num_nonzeros_counts_7x7_,
              other ? &other->model_.num_nonzeros_counts_7x7_ : nullptr,
              "NONZERO 7x7",
              {"cmp","nbr","bit","prevbits"}, spec);
    
    print_all(model_.num_nonzeros_counts_1x8_,
              other ? &other->model_.num_nonzeros_counts_1x8_ : nullptr,
              "NONZERO_1x8",
              {"cmp","eobx","num_nonzeros","bit","prevbits"}, spec);
    print_all(model_.num_nonzeros_counts_8x1_,
              other ? &other->model_.num_nonzeros_counts_8x1_ : nullptr,
              "NONZERO_8x1",
              {"cmp","eobx","num_nonzeros","bit","prevbits"}, spec);
    print_all(model_.exponent_counts_dc_,
              other ? &other->model_.exponent_counts_dc_ : nullptr,
              "EXP_DC",
              {"cmp","num_nonzeros","neigh_exp","bit","prevbits"}, spec);
    print_all(model_.exponent_counts_,
              other ? &other->model_.exponent_counts_ : nullptr,
              "EXP7x7",
              {"cmp","coef","num_nonzeros","neigh_exp","bit","prevbits"}, spec);
    print_all(model_.exponent_counts_x_,
              other ? &other->model_.exponent_counts_x_: nullptr,
              "EXP_8x1",
              {"cmp","coef","num_nonzeros","neigh_exp","bit","prevbits"}, spec);
    print_all(model_.residual_noise_counts_,
              other ? &other->model_.residual_noise_counts_: nullptr,
              "NOISE",
              {"cmp","coef","num_nonzeros","bit"}, spec);
    print_all(model_.residual_threshold_counts_,
              other ? &other->model_.residual_threshold_counts_ : nullptr,
              "THRESH8",
              {"cmp","max","exp","prevbits"}, spec);
    print_all(model_.sign_counts_,
              other ? &other->model_.sign_counts_ : nullptr,
              "SIGN",
              {"cmp","lakh","exp"}, spec);
    
    return *this;
}
