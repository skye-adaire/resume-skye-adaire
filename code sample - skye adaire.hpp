//
// Created by Skye Adaire on 2019-01-14.
//

#pragma once

#include <Meta.hpp>
#include <StaticIndexer.hpp>

namespace Core
{
    namespace Detail
    {
        template <typename ...>
        struct ComponentCount{};
        
        template <class Element, NatS... dimensions, template <class, NatS...> class Array>
        struct ComponentCount<Array<Element, dimensions...>>
        {
            static inline constexpr NatS value = Meta::product<Meta::List<Meta::Index<dimensions>...>>;
        };
        
        template <typename T>
        struct ComponentCount<T>
        {
            static inline constexpr NatS value = 1;
        };
        
        template <NatS indexLHS, NatS indexRHS, typename LHS, typename RHS>
        Meta_Inline static constexpr void assign(LHS&& lhs, RHS&& rhs)
        {
            lhs[indexLHS] = rhs;
        };
        
        template <NatS indexLHS, NatS indexRHS,
        class LHS,
        template <class, NatS...> class Array,
        NatS... dim, class T>
        Meta_Inline static constexpr void assign(LHS&& lhs, Array<T, dim...> rhs)
        {
            lhs[indexLHS] = rhs.e[indexRHS];
        };
        
        template <class...>
        struct Construct{};
        
        template <>
        struct Construct<Meta::List<>>//no more parts
        {
            template <class... Args>
            Meta_Inline static void call(Args...){}
        };
        
        template <class IndexLHS, class IndexRHS, class ...Parts>
        struct Construct<Meta::List< Meta::List<Meta::List<IndexLHS, IndexRHS>>, Parts...>>//one pair left in head part, move to next part and arg
        {
            template <class LHS, class Head, class... Tail>
            Meta_Inline static void call(LHS&& lhs, Head&& head, Tail&&... tail)
            {
                assign<IndexLHS::value, IndexRHS::value>(lhs, head);
                Construct<Meta::List<Parts...>>::call(lhs, tail...);
            }
        };
        
        template <class IndexLHS, class IndexRHS, class... Pairs, class ...Parts>
        struct Construct<Meta::List< Meta::List<Meta::List<IndexLHS, IndexRHS>, Pairs...>, Parts...>>//remaining pair in head part, pass head arg again
        {
            template <class LHS, class Head, class... Tail>
            Meta_Inline static void call(LHS&& lhs, Head&& head, Tail&&... tail)
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
            Meta_Inline static void call(NatS, RHS&&, LHS&&){}
        };
        template <class Head, class... Tail>
        struct CopySlice<Meta::List<Head, Tail...>>
        {
            template <typename RHS, typename LHS>
            Meta_Inline static void call(NatS start, RHS&& rhs, LHS&& lhs)
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
            Meta_Inline static auto call(Output&&, X&&){}
        };
        
        template <class Function, class Index, class ... Tail>
        struct UnaryTransform<Function, Meta::List<Index, Tail...>>
        {
            template <class Output, class X>
            Meta_Inline static auto call(Output&& output, X&& x)
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
            Meta_Inline static auto call(Output&&, LHS&&, RHS&&){}
        };
        
        template <class Function, class Index, class ... Tail>
        struct BinaryTransform<Function, Meta::List<Index, Tail...>>
        {
            template <class Output, class LHS, class RHS, NatS sizeLHS>
            Meta_Inline static auto call(Output&& output, LHS (&lhs)[sizeLHS], RHS&& rhs)//rhs scalar
            {
                output[Index::value] = Function::call(lhs[Index::value], rhs);
                BinaryTransform<Function, Meta::List<Tail...>>::call(output, lhs, rhs);
            }
            
            template <class Output, class LHS, class RHS, NatS sizeRHS>
            Meta_Inline static auto call(Output&& output, LHS && lhs, RHS (&rhs)[sizeRHS])//lhs scalar
            {
                output[Index::value] = Function::call(lhs, rhs[Index::value]);
                BinaryTransform<Function, Meta::List<Tail...>>::call(output, lhs, rhs);
            }
            
            template <class Output, class LHS, class RHS, NatS sizeLHS, NatS sizeRHS>
            Meta_Inline static auto call(Output&& output, LHS (&lhs)[sizeLHS], RHS (&rhs)[sizeRHS])//array-array
            {
                output[Index::value] = Function::call(lhs[Index::value], rhs[Index::value]);
                BinaryTransform<Function, Meta::List<Tail...>>::call(output, lhs, rhs);
            }
        };
    }
    
    /*
     * Array delegates all operators onto each of the elements
     *
     * It can have any number of dimensions, with indices computed at compile time
     *
     * It can be constructed from any combination of arguments that will cover all components
     *
     * All array operations use loop fusion
     */
    template <class Element, NatS... dimensions>
    class Array
    {
    public:
        
        using Dimensions = Meta::List<Meta::Index <dimensions>...>;
        
        static constexpr NatS rank = sizeof...(dimensions);
        
        static constexpr NatS size = Meta::Max::call(Meta::product<Dimensions>, 1);
        
        using IndexSet = Meta::IndexSequence <size>;//[0, 1, ... , size-1]
        using Indexer = StaticIndexer<dimensions...>;
        
        Element e[size];
        
    public:
        
        Array() = default;
        
        /*
         * The Array must be constructed with a combination of args providing the exact number of components
         */
        template
        <
        class ... Args,
        NatS componentSum = Meta::sum< Meta::List<Detail::ComponentCount<metaDecay<Args>>...> >,
        typename = metaIf< Meta::Equal::call(componentSum, size), void>
        >
        Meta_Inline explicit constexpr Array(Args &&... args)
        {
            using namespace Meta;
            
            //the number of components each arg will provide
            using CountsPerArg = List< Index<Detail::ComponentCount<metaDecay<Args>>::value>...>;
            //Detail::Print<CountsPerArg>::call();
            using IndicesRHS =
            concat<
            IndexSequence<Detail::ComponentCount<metaDecay<Args>>::value
            >...
            >;
            using IndexPairs = zip<IndexSet, IndicesRHS>;
            using IndexPart = Meta::partition<IndexPairs, CountsPerArg>;
            
            Detail::Construct<IndexPart>::call(e, args...);
        }
        
        /*
         * Assignment
         */
        template<typename ElementRHS>
        auto &operator=(Array<ElementRHS, dimensions...> const &toCopy)
        {
            Detail::UnaryTransform<Meta::Identity, IndexSet>::call(e, toCopy.e);
            return *this;
        }
        
        /*
         * Copy, different type
         */
        template<typename ElementRHS>
        Array(Array<ElementRHS, dimensions...> const &toCopy)
        {
            *this = toCopy;
        }
        
        Array(Array const &toCopy)
        {
            *this = toCopy;
        }
        
        template <class...> struct GetArray{};
        template <class... Values>
        struct GetArray<Meta::List<Values...>>
        {
            using type = Array<Element, Values::value...>;
        };
        
        /*
         * A slice returns all elements that are dependent on the input index, partial or complete
         */
        template <class... Indices,
        typename = metaIf<Meta::LessThanOrEqual::call(sizeof...(Indices), rank), void>
        >
        Meta_Inline auto slice(Indices&&... indices)
        {
            using SliceDimensions = Meta::drop<sizeof...(Indices), Dimensions>;
            using Slice = typename GetArray<SliceDimensions>::type;
            Slice slice;
            Detail::CopySlice<typename Slice::IndexSet>::call(Indexer::call(indices...), slice.e, e);
            return slice;
        }
        
        /*
         * Rank 0 implicit conversion to Element
         *
         template <typename = metaIf<rank == 0, void>>
         operator Element() const
         {
         return e[0];
         }*/
        
        ~Array() = default;
        
        /*
         * Index the array with a full or parial index
         * A partial index will provide the first element at that slice
         */
        template<class ... Indices,
        typename = metaIf<Meta::LessThanOrEqual::call(sizeof...(Indices), rank), void>
        >
        Element& operator()(Indices&&... indices)
        {
            return e[Indexer::index(indices...)];
        }
        
        template <NatS ... indices>
        Meta_Inline auto& index()
        {
            return e[Indexer::template index< indices... >()];
        }
        
        /*
         * 1 dimensional access, [0, size-1], unchecked
         */
        Meta_Inline Element& operator[](NatS index)
        {
            return e[index];
        }
        
        Meta_Inline Element const& operator[](NatS index) const
        {
            return e[index];
        }
        
        template <typename RHS>
        Meta_Inline auto & fill (RHS&& rhs)
        {
            for(Nat i = 0; i < size; i++)
            {
                e[i] = rhs;
            }
            return *this;
        }
        
        auto getSize()
        {
            return size;
        }
        
        //array-array operations
        
#define BinaryOperator(symbol, name)\
template <class ElementRHS>\
Meta_Inline auto& operator symbol (Array<ElementRHS, dimensions...> const& rhs)\
{\
Detail::BinaryTransform<Meta::name, IndexSet>::call(e, e, rhs.e);\
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
        
#define LogicalOperator(symbol, name)\
template <class ElementRHS>\
Meta_Inline auto operator symbol (Array<ElementRHS, dimensions...> const& rhs)\
{\
Array<Bool, dimensions...> result;\
Detail::BinaryTransform<Meta::name, IndexSet>::call(result.e, e, rhs.e);\
return result;\
}\

        LogicalOperator(==, Equal)
        LogicalOperator(!=, NotEqual)
        LogicalOperator(>, GreaterThan)
        LogicalOperator(>=, GreaterThanOrEqual)
        LogicalOperator(<, LessThan)
        LogicalOperator(<=, LessThanOrEqual)
        
#undef LogicalOperator
        
        //array-scalar operations
        
#define BinaryOperator(symbol, name)\
template <class RHS, typename = metaIf<Meta::Equal::call(Detail::ComponentCount<metaDecay<RHS>>::value, 1), void>>\
Meta_Inline auto& operator symbol (RHS&& rhs)\
{\
Detail::BinaryTransform<Meta::name, IndexSet>::call(e, e, rhs);\
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
        
#define LogicalOperator(symbol, name)\
template <class RHS, typename = metaIf<Meta::Equal::call(Detail::ComponentCount<metaDecay<RHS>>::value, 1), void>>\
Meta_Inline auto operator symbol (RHS&& rhs)\
{\
Array<Bool, dimensions...> result;\
Detail::BinaryTransform<Meta::name, IndexSet>::call(result.e, e, rhs);\
return result;\
}\

        LogicalOperator(==, Equal)
        LogicalOperator(!=, NotEqual)
        LogicalOperator(>, GreaterThan)
        LogicalOperator(>=, GreaterThanOrEqual)
        LogicalOperator(<, LessThan)
        LogicalOperator(<=, LessThanOrEqual)
        
#undef LogicalOperator
        
    };
    
#define UnaryOperator(symbol, name)\
template <template<class, NatS...> typename Array, class ElementLHS, NatS... dimensions>\
Meta_Inline static auto& operator symbol (\
Array<ElementLHS, dimensions...>& lhs)\
{\
Detail::UnaryTransform<Meta::name, typename Array<ElementLHS, dimensions...>::IndexSet>::call(lhs.e, lhs.e);\
return lhs;\
}\

    UnaryOperator(++, Increment)
    UnaryOperator(--, Decrement)
    
#undef UnaryOperator
    /*
     #define UnaryOperator(symbol, name)\
     template <template<class, NatS...> typename Array, class ElementLHS, NatS... dimensions>\
     Meta_Inline static auto operator symbol (\
     Array<ElementLHS, dimensions...> lhs)\
     {\
     Detail::UnaryTransform<Meta::name, typename Array<ElementLHS, dimensions...>::IndexSet>::call(lhs.e, lhs.e);\
     return lhs;\
     }\
     
     UnaryOperator(-, Negate)
     UnaryOperator(!, Not)
     UnaryOperator(~, BitNot)
     
     #undef UnaryOperator
     
     */
    
    template<template<class, NatS...> typename Array, class ElementLHS, NatS...dimensions>
    __attribute__((always_inline))inline static auto operator-(Array<ElementLHS, dimensions...> lhs)
    {
        Detail::UnaryTransform<Meta::Negate, typename Array<ElementLHS, dimensions...>::IndexSet>::call(lhs.e, lhs.e);
        return lhs;
    }
    
#define BinaryOperator(symbol)\
template <template<class, NatS...> typename Array, class ElementLHS, class RHS, NatS... dimensions>\
Meta_Inline static auto operator symbol (\
Array<ElementLHS, dimensions...> lhs, \
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
    
#define BinaryOperator(symbol, name)\
template <template<class, NatS...> typename _Array, class ElementRHS, class LHS, NatS... dimensions>\
Meta_Inline static Meta::_if< Meta::Equal::call(1, Detail::ComponentCount<LHS>::value), _Array<ElementRHS, dimensions...>> \
operator symbol (\
LHS const& lhs, \
_Array<ElementRHS, dimensions...> rhs)\
{\
Detail::BinaryTransform<Meta::name, typename _Array<ElementRHS, dimensions...>::IndexSet>::call(rhs.e, lhs, rhs.e);\
return rhs;\
}\

    BinaryOperator(+, Add)
    BinaryOperator(-, Subtract)
    BinaryOperator(*, Multiply)
    BinaryOperator(/, Divide)
    BinaryOperator(^, Power)
    BinaryOperator(%, Mod)
    BinaryOperator(<<, BitShiftLeft)
    BinaryOperator(>>, BitShiftRight)
    BinaryOperator(&, BitAnd)
    BinaryOperator(|, BitOr)
    
#undef BinaryOperator
    
}//end core

namespace Math
{
    template< typename F, template <class, NatS...> typename Array, class Element, NatS... dimensions>
    static auto transform(F&& f, Array<Element, dimensions...> const& array)
    {
        using ImageType = typename function_traits<F>::Return;
        
        Array<ImageType, dimensions...> output;
        
        for(NatS i = 0; i < Array<Element, dimensions...>::size; i++)
        {
            output[i] = f(array[i]);
        }
        
        return output;
    }
    
    template <template <class, NatS...> typename _Array, class Element, NatS... dimensions>
    static auto round(_Array<Element, dimensions...>&& a)
    {
        return transform<float(*)(float)>(std::round, a);
    }
}

namespace Format
{
    template <class Element, NatS... dimensions>
    auto ascii(Core::Array<Element, dimensions...> const& v)
    {
        std::stringstream ss;
        ss << "[";
        for(NatS i = 0; i < Core::Array<Element, dimensions...>::size; i++)
        {
            ss << v.e[i];
            if(i < Core::Array<Element, dimensions...>::size-1) ss << ", ";
        }
        ss << "]";
        return ss.str();
    }
    
    template<typename Element, NatS ... dimensions, typename Writer,
    typename = metaIf<std::is_fundamental<Element>::value, void>
    >
    inline void serialize(Core::Array<Element, dimensions...> const & source, Writer&& writer)
    {
        try
        {
            writer(&source[0], Core::Array<Element, dimensions...>::size * sizeof(Element));
        }
        catch (std::exception& e)
        {
            throw e;
        }
    }
    
    template<typename Reader, typename Element, NatS ... dimensions,
    typename = metaIf<std::is_fundamental<Element>::value, void>
    >
    inline void deserialize(Reader&& reader, Core::Array<Element, dimensions...>& destination)
    {
        try
        {
            reader(&destination[0], Core::Array<Element, dimensions...>::size * sizeof(Element));
        }
        catch (std::exception& e)
        {
            throw e;
        }
    }
    
    template<typename Element, NatS ... dimensions, typename Writer,
    typename = metaIf<not std::is_fundamental<Element>::value, void>,
    typename = void
    >
    inline void serialize(Core::Array<Element, dimensions...> const & source, Writer&& writer)
    {
        try
        {
            for(NatS i = 0; i < Core::Array<Element, dimensions...>::size; i++)
            {
                serialize(source[i], writer);
            }
        }
        catch (std::exception& e)
        {
            throw e;
        }
    }
    
    template<typename Reader, typename Element, NatS ... dimensions,
    typename = metaIf<not std::is_fundamental<Element>::value, void>,
    typename = void
    >
    inline void deserialize(Reader&& reader, Core::Array<Element, dimensions...>& destination)
    {
        try
        {
            for(NatS i = 0; i < Core::Array<Element, dimensions...>::size; i++)
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
