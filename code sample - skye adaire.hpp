/*
 * Copyright (C) Worlds Beyond 2018
 *
 * This file is subject to the terms and conditions defined in
 * file 'LICENSE.txt', which is part of this source code package.
 */

#pragma once

#include <Meta.hpp>
#include <StaticIndexer.hpp>

namespace Core
{
    namespace Detail
    {
        template <typename ...>
        struct ComponentCount{};

        template <class Element, Nat... dimensions, template <class, Nat...> class Array>
        struct ComponentCount<Array<Element, dimensions...>>
        {
            static inline constexpr Nat value = Meta::product<Meta::List<Meta::Index<dimensions>...>>;
        };

        template <typename T>
        struct ComponentCount<T>
        {
            static inline constexpr Nat value = 1;
        };

        template <typename T>
        static constexpr auto componentCount = ComponentCount<Meta::decay<T>>::value;

        template <Nat indexLHS, Nat indexRHS, typename LHS, typename RHS>
        Meta_inline static constexpr void assign(LHS&& lhs, RHS&& rhs)
        {
            if constexpr (ComponentCount<Meta::decay<RHS>>::value == 1)
            {
                lhs[indexLHS] = rhs;
            }
            else
            {
                lhs[indexLHS] = rhs.elements[indexRHS];
            }
        };

        template <class...>
        struct Construct{};

        template <>
        struct Construct<Meta::List<>>//no more parts
        {
            template <class... Args>
            Meta_inline static void call(Args...){}
        };

        template <class IndexLHS, class IndexRHS, class ...Parts>
        struct Construct<Meta::List< Meta::List<Meta::List<IndexLHS, IndexRHS>>, Parts...>>//one pair left in head part, move to next part and arg
        {
            template <class LHS, class Head, class... Tail>
            Meta_inline static void call(LHS&& lhs, Head&& head, Tail&&... tail)
            {
                assign<IndexLHS::value, IndexRHS::value>(lhs, head);
                Construct<Meta::List<Parts...>>::call(lhs, tail...);
            }
        };

        template <class IndexLHS, class IndexRHS, class... Pairs, class ...Parts>
        struct Construct<Meta::List< Meta::List<Meta::List<IndexLHS, IndexRHS>, Pairs...>, Parts...>>//remaining pair in head part, pass head arg again
        {
            template <class LHS, class Head, class... Tail>
            Meta_inline static void call(LHS&& lhs, Head&& head, Tail&&... tail)
            {
                assign<IndexLHS::value, IndexRHS::value>(lhs, head);
                Construct<Meta::List<Meta::List<Pairs...>, Parts...>>::call(lhs, head, tail...);
            }
        };

        template <class...> struct CopySlice{};
        template <>
        struct CopySlice<Meta::List<>>
        {
            template <typename RHS, typename LHS>
            Meta_inline static void call(Nat, RHS&&, LHS&&){}
        };
        template <class Head, class... Tail>
        struct CopySlice<Meta::List<Head, Tail...>>
        {
            template <typename RHS, typename LHS>
            Meta_inline static void call(Nat start, RHS&& rhs, LHS&& lhs)
            {
                rhs[Head::value] = lhs[start + Head::value];
                CopySlice<Meta::List<Tail...>>::call(start, rhs, lhs);
            }
        };

        template <class...>
        struct UnaryTransform{};

        template <class Function>
        struct UnaryTransform<Function, Meta::List<>>
        {
            template <class Output, class X>
            Meta_inline static auto call(Output&&, X&&){}
        };

        template <class Function, class Index, class ... Tail>
        struct UnaryTransform<Function, Meta::List<Index, Tail...>>
        {
            template <class Output, class X>
            Meta_inline static auto call(Output&& output, X&& x)
            {
                output[Index::value] = Function::call(x[Index::value]);
                UnaryTransform<Function, Meta::List<Tail...>>::call(output, x);
            }
        };

        template <class...>
        struct BinaryTransform{};

        template <class Function>
        struct BinaryTransform<Function, Meta::List<>>
        {
            template <class Output, class LHS, class RHS>
            Meta_inline static auto call(Output&&, LHS&&, RHS&&){}
        };

        template <class Function, class Index, class ... Tail>
        struct BinaryTransform<Function, Meta::List<Index, Tail...>>
        {
            template <class Output, class LHS, class RHS, Nat sizeLHS>
            Meta_inline static auto call(Output&& output, LHS (&lhs)[sizeLHS], RHS&& rhs)//rhs scalar
            {
                output[Index::value] = Function::call(lhs[Index::value], rhs);
                BinaryTransform<Function, Meta::List<Tail...>>::call(output, lhs, rhs);
            }

            template <class Output, class LHS, class RHS, Nat sizeRHS>
            Meta_inline static auto call(Output&& output, LHS && lhs, RHS (&rhs)[sizeRHS])//lhs scalar
            {
                output[Index::value] = Function::call(lhs, rhs[Index::value]);
                BinaryTransform<Function, Meta::List<Tail...>>::call(output, lhs, rhs);
            }

            template <class Output, class LHS, class RHS, Nat sizeLHS, Nat sizeRHS>
            Meta_inline static auto call(Output&& output, LHS (&lhs)[sizeLHS], RHS (&rhs)[sizeRHS])//array-array
            {
                output[Index::value] = Function::call(lhs[Index::value], rhs[Index::value]);
                BinaryTransform<Function, Meta::List<Tail...>>::call(output, lhs, rhs);
            }
        };
    }

    /*
     * StaticTensor delegates all operators onto each of the elements
     *
     * It can have any number of dimensions, with indices computed at compile time
     *
     * It can be constructed from any combination of arguments that will cover all components
     *
     * All array operations use loop fusion
     */
    template <class Element, Nat... dimensions>
    class StaticTensor
    {
    public:

        static constexpr Nat rank = sizeof...(dimensions);

        using Dimensions = Meta::List<Meta::Index <dimensions>...>;

        static constexpr Nat size = Meta::Max::call(Meta::product<Dimensions>, 1);

        Element elements[size];

        using IndexSet = Meta::IndexSequence <size>;//[0, 1, ... , size-1]
        using Indexer = StaticIndexer<dimensions...>;

        /*
         * Constructors
         */
    public:

        StaticTensor() = default;

        /*
         * The Array must be constructed with a combination of args providing the exact number of components
         */
        template
            <
                class ... Args,
                Nat componentSum = Meta::sum< Meta::List<Detail::ComponentCount<Meta::decay<Args>>...> >,
                typename = Meta::_if< /*Detail::ComponentCount<Element>::value == 1 && */Meta::Equal::call(componentSum, size), void>
            >
        Meta_inline explicit constexpr StaticTensor(Args &&... args)
        {
            using namespace Meta;

            //the number of components each arg will provide
            using CountsPerArg = List< Index<Detail::ComponentCount<Meta::decay<Args>>::value>...>;

            using IndicesRHS =
            Meta::concat<
                IndexSequence<Detail::ComponentCount<Meta::decay<Args>>::value
                >...
            >;
            using IndexPairs = zip<IndexSet, IndicesRHS>;
            using IndexPart = Meta::partition<IndexPairs, CountsPerArg>;

            Detail::Construct<IndexPart>::call(elements, args...);
        }

    public:

        /*
         * Assignment
         */
        template<typename ElementRHS>
        auto &operator=(StaticTensor<ElementRHS, dimensions...> const &toCopy)
        {
            Detail::UnaryTransform<Meta::Identity, IndexSet>::call(elements, toCopy.elements);
            return *this;
        }

        /*
         * Copy, different type
         */
        template<typename ElementRHS>
        StaticTensor(StaticTensor<ElementRHS, dimensions...> const &toCopy)
        {
            *this = toCopy;
        }

        StaticTensor(StaticTensor const &toCopy)
        {
            *this = toCopy;
        }

        ~StaticTensor() = default;

        /*
         * Accessors
         */
    public:

        Meta_inline auto getRank() const
        {
            return rank;
        }

        Meta_inline auto getDimension(Nat rankIndex) const
        {
            static const auto _dimensions = StaticTensor<Nat, rank>(dimensions...);
            return _dimensions[rankIndex];
        }

        Meta_inline auto getDimensions() const
        {
            return StaticTensor<Nat, rank>(dimensions...);
        }

        Meta_inline auto getSize() const
        {
            return size;
        }

        /*
         * 1 dimensional access, [0, size-1], unchecked
         */
        template<typename I>
        Meta_inline auto& operator[](I&& i)
        {
            return elements[i];
        }

        template<typename I>
        Meta_inline auto const& operator[](I&& i) const
        {
            return elements[i];
        }

        /*
         * static-rank, static-value index
         */
        template <Nat ... indices>
        Meta_inline auto& index()
        {
            return elements[Indexer::template index< indices... >()];
        }

        /*
         * static-rank, dynamic-value index
         *
         * A partial index will provide the first element at that slice
         */
        template<
            class ... Indices,
            typename = Meta::_if<Meta::LessThanOrEqual::call(sizeof...(Indices), rank), void>
            >
        Element& operator()(Indices&&... indices)
        {
            return elements[Indexer::index(indices...)];
        }

        template<
            class ... Indices,
            typename = Meta::_if<Meta::LessThanOrEqual::call(sizeof...(Indices), rank), void>
        >
        Element const& operator()(Indices&&... indices) const
        {
            return elements[Indexer::index(indices...)];
        }


        /*
         * Standard-conformant iterator
         */
        class Iterator: public std::iterator
            <
                std::random_access_iterator_tag,   // iterator_category
                Element,                      // value_type
                Nat                      // difference_type
            >
        {
            Element * elementPointer;

        public:

            explicit Iterator(Element * elementPointer) :
                elementPointer(elementPointer)
            {}

            Iterator& operator++()
            {
                elementPointer++;
                return *this;
            }

            Iterator operator++(int)
            {
                Iterator retval = *this;
                ++(*this);
                return retval;
            }

            Bool operator==(Iterator other) const
            {
                return elementPointer == other.elementPointer;
            }

            Bool operator!=(Iterator other) const
            {
                return !(*this == other);
            }

            Element& operator*() const
            {
                return *elementPointer;
            }
        };

        Iterator begin()
        {
            return Iterator(elements);
        }

        Iterator end()
        {
            return Iterator(&elements[size]);
        }

        /*
         * Slice
         */
        template <class...> struct GetArray{};
        template <class... Values>
        struct GetArray<Meta::List<Values...>>
        {
            using type = StaticTensor<Element, Values::value...>;
        };

        /*
         * A slice returns all elements that are dependent on the input index, partial or complete
         */
        template <class... Indices,
            typename = Meta::_if<Meta::LessThanOrEqual::call(sizeof...(Indices), rank), void>
        >
        Meta_inline auto slice(Indices&&... indices)
        {
            using SliceDimensions = Meta::drop<sizeof...(Indices), Dimensions>;
            using Slice = typename GetArray<SliceDimensions>::type;
            Slice slice;
            Detail::CopySlice<typename Slice::IndexSet>::call(Indexer::call(indices...), slice.elements, elements);
            return slice;
        }

        /*
         * Tensor-tensor component-wise binary assignment operators, mutating this
         */
#define BinaryOperator(symbol, name)\
        template <class ElementRHS>\
        Meta_inline auto& operator symbol (StaticTensor<ElementRHS, dimensions...> const& rhs)\
        {\
            Detail::BinaryTransform<Meta::name, IndexSet>::call(elements, elements, rhs.elements);\
            return *this;\
        }\

        BinaryOperator(+=, Add)
        BinaryOperator(-=, Subtract)
        BinaryOperator(*=, Multiply)
        BinaryOperator(/=, Divide)
        BinaryOperator(^=, Power)
        BinaryOperator(%=, Mod)
        BinaryOperator(<<=, BitShiftLeft)
        BinaryOperator(>>=, BitShiftRight)
        BinaryOperator(&=, BitAnd)
        BinaryOperator(|=, BitOr)

#undef BinaryOperator

        /*
         * Tensor-tensor component-wise comparison, returning tensor of bools
         */
#define ComparisonOperator(symbol, name)\
        template <class ElementRHS>\
        Meta_inline auto operator symbol (StaticTensor<ElementRHS, dimensions...> const& rhs)\
        {\
            StaticTensor<Bool, dimensions...> result;\
            Detail::BinaryTransform<Meta::name, IndexSet>::call(result.elements, elements, rhs.elements);\
            return result;\
        }\

        ComparisonOperator(==, Equal)
        ComparisonOperator(!=, NotEqual)
        ComparisonOperator(>, GreaterThan)
        ComparisonOperator(>=, GreaterThanOrEqual)
        ComparisonOperator(<, LessThan)
        ComparisonOperator(<=, LessThanOrEqual)

#undef ComparisonOperator

        /*
         * Tensor-scalar binary assignment operators, mutating this
         */
#define BinaryOperator(symbol, name)\
        template <\
            class RHS, \
            typename = Meta::_if<Detail::ComponentCount<Meta::decay<RHS>>::value == 1, void>>\
        Meta_inline auto& operator symbol (RHS&& rhs)\
        {\
            Detail::BinaryTransform<Meta::name, IndexSet>::call(elements, elements, rhs);\
            return *this;\
        }\

        BinaryOperator(+=, Add)
        BinaryOperator(-=, Subtract)
        BinaryOperator(*=, Multiply)
        BinaryOperator(/=, Divide)
        BinaryOperator(^=, Power)
        BinaryOperator(%=, Mod)
        BinaryOperator(<<=, BitShiftLeft)
        BinaryOperator(>>=, BitShiftRight)
        BinaryOperator(&=, BitAnd)
        BinaryOperator(|=, BitOr)

#undef BinaryOperator

        /*
         * Tensor-scalar comparison, returning a tensor of bools
         */
#define ComparisonOperator(symbol, name)\
        template <\
            class RHS, \
            typename = Meta::_if<Detail::ComponentCount<Meta::decay<RHS>>::value == 1, void>>\
        Meta_inline auto operator symbol (RHS&& rhs)\
        {\
            StaticTensor<Bool, dimensions...> result;\
            Detail::BinaryTransform<Meta::name, IndexSet>::call(result.elements, elements, rhs);\
            return result;\
        }\

        ComparisonOperator(==, Equal)
        ComparisonOperator(!=, NotEqual)
        ComparisonOperator(>, GreaterThan)
        ComparisonOperator(>=, GreaterThanOrEqual)
        ComparisonOperator(<, LessThan)
        ComparisonOperator(<=, LessThanOrEqual)

#undef ComparisonOperator

    };

    /*
     * Unary operators, mutating the input by reference
     */
#define UnaryOperator(symbol, name)\
    template <\
        template<class, Nat...> typename StaticTensor, \
        class ElementLHS, Nat... dimensions\
            >\
    Meta_inline static auto& operator symbol (StaticTensor<ElementLHS, dimensions...>& lhs)\
    {\
        using IndexSet = typename StaticTensor<ElementLHS, dimensions...>::IndexSet; \
        Detail::UnaryTransform<Meta::name, IndexSet>::call(lhs.elements, lhs.elements);\
        return lhs;\
    }\

    UnaryOperator(++, Increment)
    UnaryOperator(--, Decrement)

#undef UnaryOperator

    /*
     * Unary operators, copying input by value
     *
#define UnaryOperator(symbol, name)\
    template <template<class, Nat...> typename StaticTensor, class ElementLHS, Nat... dimensions>\
    Meta_Inline static auto operator symbol (\
        StaticTensor<ElementLHS, dimensions...> lhs)\
    {\
        Detail::UnaryTransform<Meta::name, typename StaticTensor<ElementLHS, dimensions...>::IndexSet>::call(lhs.elements, lhs.elements);\
        return lhs;\
    }\

    UnaryOperator(-, Negate)
    UnaryOperator(!, Not)
    UnaryOperator(~, BitNot)

#undef UnaryOperator
*/
    template<template<class, Nat...> typename StaticTensor, class ElementLHS, Nat...dimensions>
    Meta_function auto operator-(StaticTensor<ElementLHS, dimensions...> lhs)
    {
        using IndexSet = typename StaticTensor<ElementLHS, dimensions...>::IndexSet;
        Detail::UnaryTransform<Meta::Negate, IndexSet>::call(lhs.elements, lhs.elements);
        return lhs;
    }

    /*
     * tensor-any binary operators
     */
#define BinaryOperator(symbol)\
    template <\
        class ElementLHS, Nat... dimensions,\
        class RHS\
            >\
    Meta_inline static auto operator symbol (\
        StaticTensor<ElementLHS, dimensions...> lhs, \
        RHS const& rhs)\
    {\
        lhs symbol##= rhs;\
        return lhs;\
    }\

    BinaryOperator(+)
    BinaryOperator(-)
    BinaryOperator(*)
    BinaryOperator(/)
    BinaryOperator(^)
    BinaryOperator(%)
    BinaryOperator(<<)
    BinaryOperator(>>)
    BinaryOperator(&)
    BinaryOperator(|)

#undef BinaryOperator

    /*
     * scalar-tensor binary operators
     */
#define BinaryOperator(symbol, name)\
    template <\
        class LHS,\
        class ElementRHS, Nat... dimensions,\
        typename = Meta::_if< Detail::ComponentCount<LHS>::value == 1, void>\
        >\
    Meta_function auto operator symbol (\
        LHS const& lhs, \
        StaticTensor<ElementRHS, dimensions...> rhs)\
    {\
        using IndexSet = typename StaticTensor<ElementRHS, dimensions...>::IndexSet;\
        Detail::BinaryTransform<Meta::name, IndexSet>::call(rhs.elements, lhs, rhs.elements);\
        return rhs;\
    }\

    BinaryOperator(+, Add)
    BinaryOperator(-, Subtract)
    BinaryOperator(*, Multiply)
    BinaryOperator(/, Divide)
//    BinaryOperator(^, Power)
//    BinaryOperator(%, Mod)
//    BinaryOperator(<<, BitShiftLeft)
//    BinaryOperator(>>, BitShiftRight)
//    BinaryOperator(&, BitAnd)
//    BinaryOperator(|, BitOr)

#undef BinaryOperator

}//end core

namespace Format
{
    template <class Element, Nat... dimensions>
    auto ascii(Core::StaticTensor<Element, dimensions...> const& v)
    {
        std::stringstream ss;
        ss << "[";
        for(Nat i = 0; i < v.getSize(); i++)
        {
            ss << ascii(v.elements[i]);
            if(i < intmax_t (v.getSize())-1) ss << ", ";
        }
        ss << "]";
        return ss.str();
    }

    /*
     * Primitives, optimized
     */
    template<typename Element, Nat ... dimensions, typename Writer,
        typename = Meta::_if<std::is_fundamental<Element>::value, void>
        >
    inline void serialize(Core::StaticTensor<Element, dimensions...> const & source, Writer&& writer)
    {
        try
        {
            writer(&source[0], source.getSize() * sizeof(Element));
        }
        catch (std::exception& e)
        {
            throw e;
        }
    }

    template<typename Reader, typename Element, Nat ... dimensions,
        typename = Meta::_if<std::is_fundamental<Element>::value, void>
    >
    inline void deserialize(Reader&& reader, Core::StaticTensor<Element, dimensions...>& destination)
    {
        try
        {
            reader(&destination[0], destination.getSize() * sizeof(Element));
        }
        catch (std::exception& e)
        {
            throw e;
        }
    }

    /*
     * Non-primitives, delegated format calls
     */
    template<typename Element, Nat ... dimensions, typename Writer,
        typename = Meta::_if<not std::is_fundamental<Element>::value, void>,
            typename = void
    >
    inline void serialize(Core::StaticTensor<Element, dimensions...> const & source, Writer&& writer)
    {
        try
        {
            for(Nat i = 0; i < source.getSize(); i++)
            {
                serialize(source[i], writer);
            }
        }
        catch (std::exception& e)
        {
            throw e;
        }
    }

    template<typename Reader, typename Element, Nat ... dimensions,
        typename = Meta::_if<not std::is_fundamental<Element>::value, void>,
            typename = void
    >
    inline void deserialize(Reader&& reader, Core::StaticTensor<Element, dimensions...>& destination)
    {
        try
        {
            for(Nat i = 0; i < destination.getSize(); i++)
            {
                deserialize(reader, destination[i]);
            }
        }
        catch (std::exception& e)
        {
            throw e;
        }
    }
}