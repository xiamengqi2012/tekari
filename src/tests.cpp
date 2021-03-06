#include <tekari/common.h>
#include <iostream>

// #include <tekari/common.h>
#include <iostream>
#include <string>
#include <tekari/matrix_xx.h>
#define POWITACQ_IMPLEMENTATION
#include <tekari/powitacq.h>
#include <tekari/cie1931.h>

using namespace tekari;

#define ASSERT(cond, fmt, ...) \
    if (!(cond)) { \
        fprintf(stderr, "[Error:%s:%d] ", __func__, __LINE__); \
        fprintf(stderr, fmt, __VA_ARGS__); \
    }

template<typename T>
void check_dims(size_t rows, size_t cols, const MatrixXX<T> &m)
{
    ASSERT(m.n_cols() == cols, "got %zu should have found %zu\n", m.n_cols(), cols);
    ASSERT(m.n_rows() == rows, "got %zu should have found %zu\n", m.n_rows(), rows);
    ASSERT(m.size() == rows * cols, "got %zu should have found %zu\n", m.size(), rows * cols);
}

template<typename T>
void test_constructors(size_t rows, size_t cols, const T& v)
{
    MatrixXX<T> m(rows, cols);
    check_dims(rows, cols, m);
    MatrixXX<T> m2(rows, cols, v);
    check_dims(rows, cols, m2);
    for(size_t i = 0; i < m2.n_rows(); ++i)
        for(size_t j = 0; j < m2.n_cols(); ++j)
            ASSERT(m2[i][j] == v, "%s\n", "wrong value");
}

template<typename T>
void test_resize(size_t rows, size_t cols)
{
    MatrixXX<T> m;
    m.resize(rows, cols);
    check_dims(rows, cols, m);
}

template<typename T>
void test_assign(size_t rows, size_t cols, const T& v)
{
    MatrixXX<T> m;
    m.assign(rows, cols, v);
    check_dims(rows, cols, m);
    for(size_t i = 0; i < m.n_rows(); ++i)
        for(size_t j = 0; j < m.n_cols(); ++j)
            ASSERT(m[i][j] == v, "%s\n", "wrong value");
}

template<typename T>
void test_clear()
{
    MatrixXX<T> m(6, 10);
    check_dims(6, 10, m);
    m.clear();
    check_dims(0, 0, m);
}

void test_iterator()
{
    MatrixXX<int> m(6, 10);

    int x = 0;
    int y = 0;
    for(auto& row: m) {
        ++y;
        for(auto& item: row) {
            ++x;
            item = x * y;
        }
        x = 0;
    }
    cout << m << endl;
}

int main(int, char const* [])
{
    // test_constructors(54, 23, 2.4);
    // test_clear<float>();
    // test_resize<uint16_t>(213, 13);
    // test_assign(14, 2, 2.3);
    // test_iterator();

    // powitacq::Vector3f wi{0.0f, 0.0f, 1.0f};

    // constexpr size_t N_DIVS = 64;

    // powitacq::BRDF brdf("../resources/gold_satin_spec.bsdf");
    // MatrixXXf spectrum(N_DIVS * N_DIVS, brdf.wavelengths().size());

    // Timer<> t;

    // for (size_t theta = 0; theta < N_DIVS; ++theta)
    // {
    //     float v = float(theta + 0.5f) / N_DIVS;
    //     for (size_t phi = 0; phi < N_DIVS; ++phi)
    //     {
    //         float u = float(phi + 0.5f) / N_DIVS;
    //         powitacq::Vector3f wo;
    //         float pdf;
    //         powitacq::Spectrum s = brdf.sample({u, v}, wi, &wo, &pdf);
    //         memcpy(spectrum[theta * N_DIVS + phi].data(), &s[0], s.size() * sizeof(float));
    //     }
    // }
    // cout << "Valarray version " << time_string(t.reset()) << endl;

    // for (size_t theta = 0; theta < N_DIVS; ++theta)
    // {
    //     float v = float(theta + 0.5f) / N_DIVS;
    //     for (size_t phi = 0; phi < N_DIVS; ++phi)
    //     {
    //         float u = float(phi + 0.5f) / N_DIVS;
    //         powitacq::Vector3f wo;
    //         float pdf;
    //         brdf.sample({u, v}, wi, spectrum[theta * N_DIVS + phi].data(), &wo, &pdf);
    //     }
    // }
    // cout << "Float array version " << time_string(t.reset()) << endl;

    const size_t n_wavelengths = 20;

    for(size_t w = 0; w < n_wavelengths; ++w)
    {
        float lambda = CIE_LAMBDA_MIN + (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN) * w / (n_wavelengths-1);

        Vector3f XYZ = Vector3f{ cie_interp(cie_x, lambda), cie_interp(cie_y, lambda), cie_interp(cie_z, lambda) }
            * cie_interp(cie_d65, lambda) * (1.f / (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN));

        Vector3f rgb{ 0.0f };
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                rgb[i] += xyz_to_srgb[i][j] * XYZ[j];

        rgb[0] = to_srgb(rgb[0] * lambda);
        rgb[1] = to_srgb(rgb[1] * lambda);
        rgb[2] = to_srgb(rgb[2] * lambda);
        
        rgb = enoki::max(enoki::normalize(rgb), Vector3f{ 0.0f });

        Log(Error, "%fnm -> xyz[%f, %f, %f] -> rgb[%f, %f, %f]\n",
            lambda,
            XYZ[0], XYZ[1], XYZ[2],
            rgb[0], rgb[1], rgb[2]);
    }


    return 0;
}
