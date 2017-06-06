template <typename T>
inline
void assign_const_type(bh_constant* constant, T value) {
    //TODO: The general case should result in a meaningful compile-time error.
    std::cout << "Unsupported type [%s, " << constant->type << "] " << &value << std::endl;
}

<!--(for ctype, _bh_atype, bh_ctype, bh_enum in data)-->
    <!--(if bh_ctype != "complex64" and bh_ctype != "complex128")-->
    template <>
    inline
    void assign_const_type(bh_constant* constant, @!ctype!@ value)
    {
        constant->value.@!bh_ctype!@ = value;
        constant->type = @!bh_enum!@;
    }

    <!--(end)-->
<!--(end)-->

template <>
inline
void assign_const_type(bh_constant* constant, bh_complex64 value)
{
    constant->value.complex64 = value;
    constant->type = BH_COMPLEX64;
}

template <>
inline
void assign_const_type(bh_constant* constant, bh_complex128 value)
{
    constant->value.complex128 = value;
    constant->type = BH_COMPLEX128;
}

template <>
inline
void assign_const_type(bh_constant* constant, std::complex<float> value)
{
    constant->value.complex64.real = value.real();
    constant->value.complex64.imag = value.imag();
    constant->type = BH_COMPLEX64;
}

template <>
inline
void assign_const_type(bh_constant* constant, std::complex<double> value)
{
    constant->value.complex128.real = value.real();
    constant->value.complex128.imag = value.imag();
    constant->type = BH_COMPLEX128;
}
