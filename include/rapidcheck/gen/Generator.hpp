#pragma once

#include "rapidcheck/detail/ImplicitParam.h"
#include "rapidcheck/detail/GenerationParams.h"
#include "rapidcheck/detail/RoseNode.h"
#include "rapidcheck/detail/Quantifier.h"
#include "rapidcheck/detail/ErasedGenerator.h"

namespace rc {
namespace detail {

template<typename T> class ErasedGenerator;

template<typename T>
T pick(const gen::Generator<T> &generator)
{
    detail::ImplicitParam<detail::param::CurrentNode> currentNode;
    if (*currentNode != nullptr) {
        return std::move(
            currentNode->pick(
                detail::ErasedGenerator<T>(&generator)).template get<T>());
    } else {
        return generator.generate();
    }
}

} // namespace detail

namespace gen {

//! Generates a value.
template<typename T>
T Generator<T>::operator*() const
{ return detail::pick(*this); }

template<typename Gen>
void sample(int sz, Gen generator, uint64_t seed)
{
    using namespace detail;

    ImplicitParam<param::Size> size;
    size.let(sz);

    ImplicitParam<param::RandomEngine> randomEngine;
    RandomEngine engine(seed);
    randomEngine.let(&engine);

    show(generator(), std::cout);
    std::cout << std::endl;
}

template<typename T>
shrink::IteratorUP<T> Generator<T>::shrink(T value) const
{
    return shrink::nothing<T>();
}

template<typename Gen, typename Predicate>
class SuchThat : public Generator<GeneratedT<Gen>>
{
public:
    SuchThat(Gen generator, Predicate predicate)
        : m_generator(std::move(generator))
        , m_predicate(std::move(predicate)) {}

    GeneratedT<Gen> generate() const override
    {
        auto startSize = currentSize();
        auto size = startSize;
        while (true) { // TODO give up sometime
            auto x(*noShrink(resize(size, m_generator)));
            if (m_predicate(x))
                return x;
            size++;
            if ((size - startSize) > 100) {
                throw GenerationFailure(
                    "Gave up trying to generate value satisfying predicate");
            }
        }
    }

private:
    Gen m_generator;
    Predicate m_predicate;
};

// TODO make shrinkable?
template<typename T>
class Ranged : public Generator<T>
{
public:
    Ranged(T min, T max) : m_min(min), m_max(max) {}

    T generate() const override
    {
        if (m_max < m_min) {
            std::string msg;
            msg += "Invalid range [" + std::to_string(m_min);
            msg += ", " + std::to_string(m_max) + ")";
            throw GenerationFailure(msg);
        }

        if (m_max == m_min)
            return m_max;

        // TODO this seems a bit broken
        typedef typename std::make_unsigned<T>::type Uint;
        Uint value(*noShrink(resize(kNominalSize, arbitrary<Uint>())));
        return m_min + value % (m_max - m_min);
    }

private:
    T m_min, m_max;
};

template<typename Gen>
class Resize : public Generator<GeneratedT<Gen>>
{
public:
    Resize(int size, Gen generator)
        : m_size(size), m_generator(std::move(generator)) {}

    GeneratedT<Gen> generate() const override
    {
        detail::ImplicitParam<detail::param::Size> size;
        size.let(m_size);
        return m_generator.generate();
    }

    shrink::IteratorUP<GeneratedT<Gen>>
                       shrink(GeneratedT<Gen> value) const override
    { return m_generator.shrink(std::move(value)); }

private:
    int m_size;
    Gen m_generator;
};


template<typename Gen>
class Scale : public Generator<GeneratedT<Gen>>
{
public:
    Scale(double scale, Gen generator)
        : m_scale(scale), m_generator(std::move(generator)) {}

    GeneratedT<Gen> generate() const override
    {
        detail::ImplicitParam<detail::param::Size> size;
        size.let(*size * m_scale);
        return m_generator.generate();
    }

    shrink::IteratorUP<GeneratedT<Gen>>
    shrink(GeneratedT<Gen> value) const override
    { return m_generator.shrink(std::move(value)); }

private:
    double m_scale;
    Gen m_generator;
};


// Helper class for OneOf to be able to have a collection of generators of
// different types
template<typename ...Gens>
class Multiplexer;

template<typename Gen, typename ...Gens>
class Multiplexer<Gen, Gens...>
{
public:
    typedef GeneratedT<Gen> GeneratedType;
    static constexpr int numGenerators = sizeof...(Gens) + 1;

    static_assert(
        std::is_same<
        GeneratedT<Gen>,
            typename std::tuple_element<0,
            std::tuple<GeneratedT<Gens>...>>::type>::value,
        "All generators must have the same result type");

    Multiplexer(Gen generator, Gens... generators)
        : m_generator(std::move(generator))
        , m_multiplexer(std::move(generators)...) {}

    GeneratedT<Gen> pickWithId(int id) const
    {
        if (id == myId)
            return *m_generator;
        else
            return m_multiplexer.pickWithId(id);
    }

private:
    static constexpr int myId = sizeof...(Gens);

    Gen m_generator;
    Multiplexer<Gens...> m_multiplexer;
};

template<typename Gen>
class Multiplexer<Gen>
{
public:
    typedef GeneratedT<Gen> GeneratedType;
    static constexpr int numGenerators = 1;

    Multiplexer(Gen generator)
        : m_generator(std::move(generator)) {}

    GeneratedT<Gen> pickWithId(int id) const
    { return *m_generator; }

private:
    static constexpr int myId = 0;

    Gen m_generator;
};

template<typename ...Gens>
class OneOf : public Generator<GeneratedT<Multiplexer<Gens...>>>
{
public:
    OneOf(Gens... generators) : m_multiplexer(std::move(generators)...) {}

    GeneratedT<Multiplexer<Gens...>> generate() const override
    {
        int n = Multiplexer<Gens...>::numGenerators;
        auto id = *resize(kNominalSize, ranged<int>(0, n));
        return m_multiplexer.pickWithId(id);
    }

private:
    Multiplexer<Gens...> m_multiplexer;
};

// Generators of this form are common, let's not repeat ourselves
#define IMPLEMENT_SUCH_THAT_GEN(GeneratorName, predicate)               \
    template<typename T>                                                \
    class GeneratorName : public Generator<T>                           \
    {                                                                   \
    public:                                                             \
        T generate() const                                            \
        { return *suchThat<T>([](T x) { return (predicate); }); }  \
    };

IMPLEMENT_SUCH_THAT_GEN(NonZero, x != 0)
IMPLEMENT_SUCH_THAT_GEN(Positive, x > 0)
IMPLEMENT_SUCH_THAT_GEN(Negative, x < 0)
IMPLEMENT_SUCH_THAT_GEN(NonNegative, x >= 0)

#undef IMPLEMENT_SUCH_THAT_GEN

template<typename Container, typename Gen>
class Vector : public Generator<Container>
{
public:
    explicit Vector(std::size_t size, Gen generator)
        : m_size(size)
        , m_generator(std::move(generator)) {}

    Container generate() const override
    {
        detail::CollectionBuilder<Container> builder;
        auto gen = noShrink(m_generator);
        for (int i = 0; i < m_size; i++) {
            // Gradually increase size for every failed adding attempt to
            // increase likelihood that we get a "successful" value the next
            // time
            auto startSize = gen::currentSize();
            auto currentSize = startSize;
            while (!builder.add(*resize(currentSize, gen))) {
                currentSize++;
                if ((currentSize - startSize) > 100) {
                    std::ostringstream msg;
                    msg << "Gave up trying to generate value that can be added";
                    msg << "to container of type '";
                    detail::showType<Container>(msg);
                    msg << "'";
                    throw GenerationFailure(msg.str());
                }
            }
        }
        return std::move(builder.collection());
    }

    shrink::IteratorUP<Container> shrink(Container value) const override
    { return shrink(value, detail::IsCopyConstructible<Container>()); }

private:
    shrink::IteratorUP<Container> shrink(const Container &value,
                                         std::false_type) const
    {
        return shrink::nothing<Container>();
    }

    shrink::IteratorUP<Container> shrink(const Container &value,
                                         std::true_type) const
    {
        return shrink::eachElement(
            value,
            [=](typename Container::value_type element) {
                return m_generator.shrink(std::move(element));
            });
    }

    std::size_t m_size;
    Gen m_generator;
};

template<typename Container, typename Gen>
class Collection : public Generator<Container>
{
public:
    explicit Collection(Gen generator)
        : m_generator(std::move(generator)) {}

    Container generate() const override
    {
        typedef typename Container::size_type SizeT;
        auto size = *ranged<SizeT>(0, currentSize() + 1);
        detail::CollectionBuilder<Container> builder;
        for (int i = 0; i < size; i++)
            builder.add(*noShrink(m_generator));
        return std::move(builder.collection());
    }

    shrink::IteratorUP<Container> shrink(Container value) const override
    { return shrink(value, detail::IsCopyConstructible<Container>()); }

private:
    shrink::IteratorUP<Container> shrink(const Container &value,
                                         std::false_type) const
    {
        return shrink::nothing<Container>();
    }

    shrink::IteratorUP<Container> shrink(const Container &value,
                                         std::true_type) const
    {
        return shrink::sequentially(
            shrink::removeChunks(value),
            shrink::eachElement(
                value,
                [=](typename Container::value_type element) {
                    return m_generator.shrink(std::move(element));
                }));
    }

    Gen m_generator;
};

// Specialization for std::array. T must be default constructible.
template<typename T, std::size_t N, typename Gen>
class Collection<std::array<T, N>, Gen> : public Generator<std::array<T, N>>
{
public:
    typedef std::array<T, N> ArrayT;

    static_assert(std::is_default_constructible<T>::value,
                  "T must be default constructible.");

    explicit Collection(Gen generator)
        : m_generator(std::move(generator)) {}

    ArrayT generate() const override
    {
        ArrayT array;
        for (std::size_t i = 0; i < N; i++)
            array[i] = *noShrink(m_generator);
        return std::move(array);
    }

    shrink::IteratorUP<ArrayT> shrink(ArrayT value) const override
    { return shrink(value, detail::IsCopyConstructible<ArrayT>()); }

private:
    shrink::IteratorUP<ArrayT> shrink(const ArrayT &value,
                                      std::false_type) const
    {
        return shrink::nothing<ArrayT>();
    }

    shrink::IteratorUP<ArrayT> shrink(const ArrayT &value,
                                      std::true_type) const
    {
        return shrink::eachElement(
            value,
            [=](typename ArrayT::value_type element) {
                return m_generator.shrink(std::move(element));
            });
    }

    Gen m_generator;
};

template<typename Callable>
class AnyInvocation : public Generator<
    typename detail::Quantifier<Callable>::ReturnType>
{
public:
    explicit AnyInvocation(Callable callable)
        : m_quantifier(std::move(callable)) {}

    typename detail::Quantifier<Callable>::ReturnType
    generate() const override
    { return m_quantifier(); }

private:
    detail::Quantifier<Callable> m_quantifier;
};

template<typename T>
class Constant : public Generator<T>
{
public:
    explicit Constant(T value) : m_value(std::move(value)) {}
    T generate() const override { return m_value; }

private:
    T m_value;
};

// First of all overrides param::Shrink so that children do not implicitly
// shrink but since it also does not implement `shrink` which means it
// does not itself shrink
template<typename Gen>
class NoShrink : public Generator<GeneratedT<Gen>>
{
public:
    explicit NoShrink(Gen generator) : m_generator(std::move(generator)) {}
    GeneratedT<Gen> generate() const override
    {
        detail::ImplicitParam<detail::param::NoShrink> noShrink;
        noShrink.let(true);
        return m_generator.generate();
    }

private:
    Gen m_generator;
};

template<typename Gen, typename Mapper>
class Mapped : public Generator<
    typename std::result_of<Mapper(GeneratedT<Gen>)>::type>
{
public:
    typedef typename
    std::result_of<Mapper(GeneratedT<Gen>)>::type T;

    Mapped(Gen generator, Mapper mapper)
        : m_generator(std::move(generator))
        , m_mapper(std::move(mapper)) {}

    T generate() const override { return m_mapper(*m_generator); }

private:
    Gen m_generator;
    Mapper m_mapper;
};

template<typename T>
class Character : public Generator<T>
{
public:
    T generate() const override
    {
        return *oneOf(map(ranged<uint8_t>(1, 128),
                          [](uint8_t x) { return static_cast<T>(x); }),
                      nonZero<T>());
    }

    shrink::IteratorUP<T> shrink(T value) const override
    {
        // TODO this can probably be better
        std::vector<T> chars;
        switch (value) {
        default:
            chars.insert(chars.begin(), static_cast<T>('3'));
        case '3':
            chars.insert(chars.begin(), static_cast<T>('2'));
        case '2':
            chars.insert(chars.begin(), static_cast<T>('1'));
        case '1':
            chars.insert(chars.begin(), static_cast<T>('C'));
        case 'C':
            chars.insert(chars.begin(), static_cast<T>('B'));
        case 'B':
            chars.insert(chars.begin(), static_cast<T>('A'));
        case 'A':
            chars.insert(chars.begin(), static_cast<T>('c'));
        case 'c':
            chars.insert(chars.begin(), static_cast<T>('b'));
        case 'b':
            chars.insert(chars.begin(), static_cast<T>('a'));
        case 'a':
            ;
        }

        return shrink::constant<T>(chars);
    }
};

template<typename Exception, typename Gen, typename Catcher>
class Rescue : public Generator<GeneratedT<Gen>>
{
public:
    Rescue(Gen generator, Catcher catcher)
        : m_generator(generator), m_catcher(catcher) {}

    GeneratedT<Gen> generate() const override
    {
        try {
            return m_generator.generate();
        } catch (const Exception &e) {
            return m_catcher(e);
        }
    }

private:
    Gen m_generator;
    Catcher m_catcher;
};

template<typename Callable>
class Lambda : public Generator<typename std::result_of<Callable()>::type>
{
public:
    explicit Lambda(Callable callable) : m_callable(std::move(callable)) {}

    typename std::result_of<Callable()>::type
    generate() const override { return m_callable(); }

private:
    Callable m_callable;
};

template<typename ...Gens>
class TupleOf;

template<>
class TupleOf<> : public Generator<std::tuple<>>
{
public:
    std::tuple<> generate() const override { return std::tuple<>(); }
};

template<typename Gen, typename ...Gens>
class TupleOf<Gen, Gens...>
    : public Generator<std::tuple<GeneratedT<Gen>,
                                  GeneratedT<Gens>...>>
{
public:
    typedef std::tuple<GeneratedT<Gen>,
                       GeneratedT<Gens>...> TupleT;
    typedef GeneratedT<Gen> HeadT;
    typedef std::tuple<GeneratedT<Gens>...> TailT;

    TupleOf(Gen headGenerator, Gens ...tailGenerators)
        : m_headGenerator(std::move(headGenerator))
        , m_tailGenerator(std::move(tailGenerators)...) {}

    TupleT generate() const override
    {
        return std::tuple_cat(
            std::tuple<GeneratedT<Gen>>(*m_headGenerator),
            *m_tailGenerator);
    }

    shrink::IteratorUP<TupleT> shrink(TupleT value) const override
    { return shrink(value, detail::IsCopyConstructible<TupleT>()); }

private:
    shrink::IteratorUP<TupleT> shrink(const TupleT &value,
                                      std::false_type) const
    { return shrink::nothing<TupleT>(); }

    shrink::IteratorUP<TupleT> shrink(const TupleT &value,
                                      std::true_type) const
    {
        // Shrink the head and map it by append the unshrunk tail,
        // then shrink the tail and map it by prepending the unshrink head.
        return shrink::sequentially(
            shrink::map(m_headGenerator.shrink(std::get<0>(value)),
                        [=] (HeadT &&x) -> TupleT {
                            return std::tuple_cat(
                                std::tuple<HeadT>(std::move(x)),
                                detail::tupleTail(value));
                        }),
            shrink::map(m_tailGenerator.shrink(detail::tupleTail(value)),
                        [=] (TailT &&x) -> TupleT {
                            return std::tuple_cat(
                                std::tuple<HeadT>(std::get<0>(value)),
                                std::move(x));
                        }));
    }

    Gen m_headGenerator;
    TupleOf<Gens...> m_tailGenerator;
};

template<typename Gen1, typename Gen2>
class PairOf : public Generator<std::pair<GeneratedT<Gen1>,
                                          GeneratedT<Gen2>>>
{
public:
    typedef GeneratedT<Gen1> T1;
    typedef GeneratedT<Gen2> T2;
    typedef typename std::pair<T1, T2> PairT;

    PairOf(Gen1 generator1, Gen2 generator2)
        : m_generator(std::move(generator1),
                      std::move(generator2)) {}

    PairT generate() const override
    {
        auto x = m_generator.generate();
        return PairT(std::move(std::get<0>(x)),
                     std::move(std::get<1>(x)));
    }

    shrink::IteratorUP<PairT> shrink(PairT value) const override
    {
        return shrink::map(
            m_generator.shrink(std::tuple<T1, T2>(std::move(value.first),
                                                  std::move(value.second))),
            [] (std::tuple<T1, T2> &&x) {
                return PairT(std::move(std::get<0>(x)),
                             std::move(std::get<1>(x)));
            });
    }

private:
    TupleOf<Gen1, Gen2> m_generator;
};

//
// Factory functions
//

template<typename T>
Arbitrary<T> arbitrary() { return Arbitrary<T>(); }

template<typename Generator, typename Predicate>
SuchThat<Generator, Predicate> suchThat(Generator gen,
                                        Predicate pred)
{ return SuchThat<Generator, Predicate>(std::move(gen), std::move(pred)); }

template<typename T, typename Predicate>
SuchThat<Arbitrary<T>, Predicate> suchThat(Predicate pred)
{ return suchThat(arbitrary<T>(), std::move(pred)); }

template<typename T>
Ranged<T> ranged(T min, T max)
{
    static_assert(std::is_arithmetic<T>::value,
                  "ranged only supports arithmetic types");
    return Ranged<T>(min, max);
}

template<typename ...Gens>
OneOf<Gens...> oneOf(Gens... generators)
{
    return OneOf<Gens...>(std::move(generators)...);
}

template<typename T>
NonZero<T> nonZero()
{ return NonZero<T>(); }

template<typename T>
Positive<T> positive()
{ return Positive<T>(); }

template<typename T>
Negative<T> negative()
{
    static_assert(std::is_signed<T>::value,
                  "gen::negative can only be used for signed types");
    return Negative<T>();
}

template<typename T>
NonNegative<T> nonNegative()
{ return NonNegative<T>(); }

template<typename Container, typename Gen>
Vector<Container, Gen> vector(std::size_t size, Gen gen)
{ return Vector<Container, Gen>(size, std::move(gen)); }

template<typename Coll, typename Gen>
Collection<Coll, Gen> collection(Gen gen)
{ return Collection<Coll, Gen>(std::move(gen)); }

template<typename Gen>
Resize<Gen> resize(int size, Gen gen)
{ return Resize<Gen>(size, std::move(gen)); }

template<typename Gen>
Scale<Gen> scale(double scale, Gen gen)
{ return Scale<Gen>(scale, std::move(gen)); }

template<typename Callable>
AnyInvocation<Callable> anyInvocation(Callable callable)
{ return AnyInvocation<Callable>(std::move(callable)); }

template<typename Gen>
NoShrink<Gen> noShrink(Gen generator)
{ return NoShrink<Gen>(std::move(generator)); }

template<typename Gen, typename Mapper>
Mapped<Gen, Mapper> map(Gen generator, Mapper mapper)
{ return Mapped<Gen, Mapper>(std::move(generator), std::move(mapper)); }

template<typename T>
Character<T> character() { return Character<T>(); }

// TODO deduce exception type
template<typename Exception, typename Gen, typename Catcher>
Rescue<Exception, Gen, Catcher> rescue(Gen generator, Catcher catcher)
{
    return Rescue<Exception, Gen, Catcher>(std::move(generator),
                                           std::move(catcher));
}

template<typename T>
Constant<T> constant(T value) { return Constant<T>(std::move(value)); }

template<typename Callable>
Lambda<Callable> lambda(Callable callable)
{ return Lambda<Callable>(std::move(callable)); }

template<typename ...Gens>
TupleOf<Gens...> tupleOf(Gens ...generators)
{ return TupleOf<Gens...>(std::move(generators)...); }

template<typename Gen1, typename Gen2>
PairOf<Gen1, Gen2> pairOf(Gen1 generator1, Gen2 generator2)
{ return PairOf<Gen1, Gen2>(std::move(generator1), std::move(generator2)); }

} // namespace gen
} // namespace rc

#include "Arbitrary.hpp"