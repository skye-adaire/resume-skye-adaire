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
            lhs[indexLHS] = rhs.elements[indexRHS];
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
    class StaticTensor
    {
    public:
        
        static constexpr NatS rank = sizeof...(dimensions);
        
        using Dimensions = Meta::List<Meta::Index <dimensions>...>;
        
        static constexpr NatS size = Meta::Max::call(Meta::product<Dimensions>, 1);
        
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
        NatS componentSum = Meta::sum< Meta::List<Detail::ComponentCount<metaDecay<Args>>...> >,
        typename = metaIf< Meta::Equal::call(componentSum, size), void>
        >
        Meta_Inline explicit constexpr StaticTensor(Args &&... args)
        {
            using namespace Meta;
            
            //the number of components each arg will provide
            using CountsPerArg = List< Index<Detail::ComponentCount<metaDecay<Args>>::value>...>;
            
            using IndicesRHS =
            concat<
            IndexSequence<Detail::ComponentCount<metaDecay<Args>>::value
            >...
            >;
            using IndexPairs = zip<IndexSet, IndicesRHS>;
            using IndexPart = Meta::partition<IndexPairs, CountsPerArg>;
            
            Detail::Construct<IndexPart>::call(elements, args...);
        }
        
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
        
        Meta_Inline auto getRank() const
        {
            return rank;
        }
        
        Meta_Inline auto getDimension(NatS rankIndex) const
        {
            static const auto _dimensions = StaticTensor<NatS, rank>(dimensions...);
            return _dimensions[rankIndex];
        }
        
        Meta_Inline auto getDimensions() const
        {
            return StaticTensor<NatS, rank>(dimensions...);
        }
        
        Meta_Inline auto getSize() const
        {
            return size;
        }
        
        /*
         * 1 dimensional access, [0, size-1], unchecked
         */
        template<typename I>
        Meta_Inline auto& operator[](I&& i)
        {
            return elements[i];
        }
        
        template<typename I>
        Meta_Inline auto const& operator[](I&& i) const
        {
            return elements[i];
        }
        
        /*
         * static-rank, static-value index
         */
        template <NatS ... indices>
        Meta_Inline auto& index()
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
        typename = metaIf<Meta::LessThanOrEqual::call(sizeof...(Indices), rank), void>
        >
        Element& operator()(Indices&&... indices)
        {
            return elements[Indexer::index(indices...)];
        }
        
        /*
         * Standard-conformant iterator
         */
        class Iterator: public std::iterator
        <
        std::input_iterator_tag,   // iterator_category
        Element,                      // value_type
        NatS                      // difference_type
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
            
            Iterator operator++(Int)
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
        typename = metaIf<Meta::LessThanOrEqual::call(sizeof...(Indices), rank), void>
        >
        Meta_Inline auto slice(Indices&&... indices)
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
Meta_Inline auto& operator symbol (StaticTensor<ElementRHS, dimensions...> const& rhs)\
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
Meta_Inline auto operator symbol (StaticTensor<ElementRHS, dimensions...> const& rhs)\
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
template <class RHS, typename = metaIf<Meta::Equal::call(Detail::ComponentCount<metaDecay<RHS>>::value, 1), void>>\
Meta_Inline auto& operator symbol (RHS&& rhs)\
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
typename = metaIf<Meta::Equal::call(Detail::ComponentCount<metaDecay<RHS>>::value, 1), void>>\
Meta_Inline auto operator symbol (RHS&& rhs)\
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
template <template<class, NatS...> typename StaticTensor, class ElementLHS, NatS... dimensions>\
Meta_Inline static auto& operator symbol (\
StaticTensor<ElementLHS, dimensions...>& lhs)\
{\
Detail::UnaryTransform<Meta::name, typename StaticTensor<ElementLHS, dimensions...>::IndexSet>::call(lhs.elements, lhs.elements);\
return lhs;\
}\

    UnaryOperator(++, Increment)
    UnaryOperator(--, Decrement)
    
#undef UnaryOperator
    
    /*
     * Unary operators, copying input by value
     *
     #define UnaryOperator(symbol, name)\
     template <template<class, NatS...> typename StaticTensor, class ElementLHS, NatS... dimensions>\
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
    template<template<class, NatS...> typename StaticTensor, class ElementLHS, NatS...dimensions>
    __attribute__((always_inline)) inline static auto
    operator-(StaticTensor<ElementLHS, dimensions...> lhs)
    {
        Detail::UnaryTransform<Meta::Negate, typename StaticTensor<ElementLHS, dimensions...>::IndexSet>::call(lhs.elements,
                                                                                                               lhs.elements);
        return lhs;
    }
    
    /*
     * tensor-scalar binary operators
     */
#define BinaryOperator(symbol)\
template <template<class, NatS...> typename StaticTensor, class ElementLHS, class RHS, NatS... dimensions>\
Meta_Inline static auto operator symbol (\
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
template <template<class, NatS...> typename _StaticTensor, class ElementRHS, class LHS, NatS... dimensions>\
Meta_Inline static Meta::_if< Meta::Equal::call(1, Detail::ComponentCount<LHS>::value), _StaticTensor<ElementRHS, dimensions...>> \
operator symbol (\
LHS const& lhs, \
_StaticTensor<ElementRHS, dimensions...> rhs)\
{\
Detail::BinaryTransform<Meta::name, typename _StaticTensor<ElementRHS, dimensions...>::IndexSet>::call(rhs.elements, lhs, rhs.elements);\
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

namespace Format
{
    template <class Element, NatS... dimensions>
    auto ascii(Core::StaticTensor<Element, dimensions...> const& v)
    {
        std::stringstream ss;
        ss << "[";
        for(NatS i = 0; i < Core::StaticTensor<Element, dimensions...>::size; i++)
        {
            ss << v.elements[i];
            if(i < Core::StaticTensor<Element, dimensions...>::size-1) ss << ", ";
        }
        ss << "]";
        return ss.str();
    }
    
    /*
     * Primitives, optimized
     */
    template<typename Element, NatS ... dimensions, typename Writer,
    typename = metaIf<std::is_fundamental<Element>::value, void>
    >
    inline void serialize(Core::StaticTensor<Element, dimensions...> const & source, Writer&& writer)
    {
        try
        {
            writer(&source[0], Core::StaticTensor<Element, dimensions...>::size * sizeof(Element));
        }
        catch (std::exception& e)
        {
            throw e;
        }
    }
    
    template<typename Reader, typename Element, NatS ... dimensions,
    typename = metaIf<std::is_fundamental<Element>::value, void>
    >
    inline void deserialize(Reader&& reader, Core::StaticTensor<Element, dimensions...>& destination)
    {
        try
        {
            reader(&destination[0], Core::StaticTensor<Element, dimensions...>::size * sizeof(Element));
        }
        catch (std::exception& e)
        {
            throw e;
        }
    }
    
    /*
     * Non-primitives, delegated format calls
     */
    template<typename Element, NatS ... dimensions, typename Writer,
    typename = metaIf<not std::is_fundamental<Element>::value, void>,
    typename = void
    >
    inline void serialize(Core::StaticTensor<Element, dimensions...> const & source, Writer&& writer)
    {
        try
        {
            for(NatS i = 0; i < Core::StaticTensor<Element, dimensions...>::size; i++)
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
    inline void deserialize(Reader&& reader, Core::StaticTensor<Element, dimensions...>& destination)
    {
        try
        {
            for(NatS i = 0; i < Core::StaticTensor<Element, dimensions...>::size; i++)
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
