#include <madness_config.h>

#ifdef MADNESS_HAS_ELEMENTAL

#include <linalg/elem.h>

namespace madness {
    template
    void gesvp(World& world, const Tensor<double>& a, const Tensor<double>& b, Tensor<double>& x);

    template
    void sygvp(World& world, const Tensor<double>& A, const Tensor<double>& B, int itype,
              Tensor<double>& V, Tensor<double>& e);
}

#endif //MADNESS_HAS_ELEMENTAL