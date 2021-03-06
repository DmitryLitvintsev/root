// Author: Enrico Guiraud, Danilo Piparo CERN  02/2018

/*************************************************************************
 * Copyright (C) 1995-2016, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TDF_TINTERFACE_UTILS
#define ROOT_TDF_TINTERFACE_UTILS

#include <ROOT/TDFActionHelpers.hxx> // for BuildAndBook
#include <ROOT/TDFNodes.hxx>
#include <ROOT/TDFUtils.hxx>
#include <ROOT/TypeTraits.hxx>
#include <ROOT/TSeq.hxx>
#include <ROOT/RStringView.hxx>
#include <algorithm>
#include <deque>
#include <functional>
#include <initializer_list>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <vector>

#include "RtypesCore.h"
#include "TError.h"
#include "TH1.h"

class TObjArray;
class TTree;
namespace ROOT {
namespace Experimental {
namespace TDF {
class TDataSource;
} // namespace TDF
template <int D, typename P, template <int, typename, template <typename> class> class... S>
class THist;
} // namespace Experimental
} // namespace ROOT

/// \cond HIDDEN_SYMBOLS

namespace ROOT {
namespace Internal {
namespace TDF {
using namespace ROOT::Detail::TDF;
using namespace ROOT::Experimental::TDF;
namespace TTraits = ROOT::TypeTraits;

/// An helper object that sets and resets gErrorIgnoreLevel via RAII.
class TIgnoreErrorLevelRAII {
private:
   int fCurIgnoreErrorLevel = gErrorIgnoreLevel;

public:
   TIgnoreErrorLevelRAII(int errorIgnoreLevel) { gErrorIgnoreLevel = errorIgnoreLevel; }
   TIgnoreErrorLevelRAII() { gErrorIgnoreLevel = fCurIgnoreErrorLevel; }
};

template <typename T>
class CacheColumnHolder {
public:
   using value_type = T; // A shortcut to the value_type of the vector
   std::vector<value_type> fContent;
   // This method returns a pointer since we treat these columns as if they come
   // from a data source, i.e. we forward an entry point to valid memory rather
   // than a value.
   value_type *operator()(unsigned int /*slot*/, ULong64_t iEvent) { return &fContent[iEvent]; };
};

/****** BuildAndBook overloads *******/
// BuildAndBook builds a TAction with the right operation and books it with the TLoopManager

// clang-format off
/// This namespace defines types to be used for tag dispatching in TInterface.
namespace ActionTypes {
// they cannot just be forward declared: we need concrete types for jitting and to use them with TClass::GetClass
struct Histo1D {};
struct Histo2D {};
struct Histo3D {};
struct Profile1D {};
struct Profile2D {};
struct Min {};
struct Max {};
struct Sum {};
struct Mean {};
struct Fill {};
}
// clang-format on

/// Check whether a histogram type is a classic or v7 histogram.
template <typename T>
struct IsV7Hist : public std::false_type {
   static_assert(std::is_base_of<TH1, T>::value, "not implemented for this type");
};

template <int D, typename P, template <int, typename, template <typename> class> class... S>
struct IsV7Hist<ROOT::Experimental::THist<D, P, S...>> : public std::true_type {
};

template <typename T, bool ISV7HISTO = IsV7Hist<T>::value>
struct HistoUtils {
   static void SetCanExtendAllAxes(T &h) { h.SetCanExtend(::TH1::kAllAxes); }
   static bool HasAxisLimits(T &h)
   {
      auto xaxis = h.GetXaxis();
      return !(xaxis->GetXmin() == 0. && xaxis->GetXmax() == 0.);
   }
};

template <typename T>
struct HistoUtils<T, true> {
   static void SetCanExtendAllAxes(T &) {}
   static bool HasAxisLimits(T &) { return true; }
};

// Generic filling (covers Histo2D, Histo3D, Profile1D and Profile2D actions, with and without weights)
template <typename... BranchTypes, typename ActionType, typename ActionResultType, typename PrevNodeType>
TActionBase *BuildAndBook(const ColumnNames_t &bl, const std::shared_ptr<ActionResultType> &h,
                          const unsigned int nSlots, TLoopManager &loopManager, PrevNodeType &prevNode, ActionType *)
{
   using Helper_t = FillTOHelper<ActionResultType>;
   using Action_t = TAction<Helper_t, PrevNodeType, TTraits::TypeList<BranchTypes...>>;
   auto action = std::make_shared<Action_t>(Helper_t(h, nSlots), bl, prevNode);
   loopManager.Book(action);
   return action.get();
}

// Histo1D filling (must handle the special case of distinguishing FillTOHelper and FillHelper
template <typename... BranchTypes, typename PrevNodeType>
TActionBase *BuildAndBook(const ColumnNames_t &bl, const std::shared_ptr<::TH1D> &h, const unsigned int nSlots,
                          TLoopManager &loopManager, PrevNodeType &prevNode, ActionTypes::Histo1D *)
{
   auto hasAxisLimits = HistoUtils<::TH1D>::HasAxisLimits(*h);

   TActionBase *actionBase;
   if (hasAxisLimits) {
      using Helper_t = FillTOHelper<::TH1D>;
      using Action_t = TAction<Helper_t, PrevNodeType, TTraits::TypeList<BranchTypes...>>;
      auto action = std::make_shared<Action_t>(Helper_t(h, nSlots), bl, prevNode);
      loopManager.Book(action);
      actionBase = action.get();
   } else {
      using Helper_t = FillHelper;
      using Action_t = TAction<Helper_t, PrevNodeType, TTraits::TypeList<BranchTypes...>>;
      auto action = std::make_shared<Action_t>(Helper_t(h, nSlots), bl, prevNode);
      loopManager.Book(action);
      actionBase = action.get();
   }

   return actionBase;
}

// Min action
template <typename BranchType, typename PrevNodeType, typename ActionResultType>
TActionBase *
BuildAndBook(const ColumnNames_t &bl, const std::shared_ptr<ActionResultType> &minV, const unsigned int nSlots,
             TLoopManager &loopManager, PrevNodeType &prevNode, ActionTypes::Min *)
{
   using Helper_t = MinHelper<ActionResultType>;
   using Action_t = TAction<Helper_t, PrevNodeType, TTraits::TypeList<BranchType>>;
   auto action = std::make_shared<Action_t>(Helper_t(minV, nSlots), bl, prevNode);
   loopManager.Book(action);
   return action.get();
}

// Max action
template <typename BranchType, typename PrevNodeType, typename ActionResultType>
TActionBase *
BuildAndBook(const ColumnNames_t &bl, const std::shared_ptr<ActionResultType> &maxV, const unsigned int nSlots,
             TLoopManager &loopManager, PrevNodeType &prevNode, ActionTypes::Max *)
{
   using Helper_t = MaxHelper<ActionResultType>;
   using Action_t = TAction<Helper_t, PrevNodeType, TTraits::TypeList<BranchType>>;
   auto action = std::make_shared<Action_t>(Helper_t(maxV, nSlots), bl, prevNode);
   loopManager.Book(action);
   return action.get();
}

// Sum action
template <typename BranchType, typename PrevNodeType, typename ActionResultType>
TActionBase *
BuildAndBook(const ColumnNames_t &bl, const std::shared_ptr<ActionResultType> &sumV, const unsigned int nSlots,
             TLoopManager &loopManager, PrevNodeType &prevNode, ActionTypes::Sum *)
{
   using Helper_t = SumHelper<ActionResultType>;
   using Action_t = TAction<Helper_t, PrevNodeType, TTraits::TypeList<BranchType>>;
   auto action = std::make_shared<Action_t>(Helper_t(sumV, nSlots), bl, prevNode);
   loopManager.Book(action);
   return action.get();
}

// Mean action
template <typename BranchType, typename PrevNodeType>
TActionBase *BuildAndBook(const ColumnNames_t &bl, const std::shared_ptr<double> &meanV, const unsigned int nSlots,
                          TLoopManager &loopManager, PrevNodeType &prevNode, ActionTypes::Mean *)
{
   using Helper_t = MeanHelper;
   using Action_t = TAction<Helper_t, PrevNodeType, TTraits::TypeList<BranchType>>;
   auto action = std::make_shared<Action_t>(Helper_t(meanV, nSlots), bl, prevNode);
   loopManager.Book(action);
   return action.get();
}
/****** end BuildAndBook ******/

std::vector<std::string> FindUsedColumnNames(std::string_view, TObjArray *, const std::vector<std::string> &);

using TmpBranchBasePtr_t = std::shared_ptr<TCustomColumnBase>;

Long_t JitTransformation(void *thisPtr, std::string_view methodName, std::string_view interfaceTypeName,
                         std::string_view name, std::string_view expression,
                         const std::map<std::string, std::string> &aliasMap, const ColumnNames_t &branches,
                         const std::vector<std::string> &customColumns,
                         const std::map<std::string, TmpBranchBasePtr_t> &tmpBookedBranches, TTree *tree,
                         std::string_view returnTypeName, TDataSource *ds);

std::string JitBuildAndBook(const ColumnNames_t &bl, const std::string &prevNodeTypename, void *prevNode,
                            const std::type_info &art, const std::type_info &at, const void *r, TTree *tree,
                            const unsigned int nSlots, const std::map<std::string, TmpBranchBasePtr_t> &customColumns,
                            TDataSource *ds, const std::shared_ptr<TActionBase *> *const actionPtrPtr);

// allocate a shared_ptr on the heap, return a reference to it. the user is responsible of deleting the shared_ptr*.
// this function is meant to only be used by TInterface's action methods, and should be deprecated as soon as we find
// a better way to make jitting work: the problem it solves is that we need to pass the same shared_ptr to the Helper
// object of each action and to the TResultProxy returned by the action. While the former is only instantiated when
// the event loop is about to start, the latter has to be returned to the user as soon as the action is booked.
// a heap allocated shared_ptr will stay alive long enough that at jitting time its address is still valid.
template <typename T>
std::shared_ptr<T> *MakeSharedOnHeap(const std::shared_ptr<T> &shPtr)
{
   return new std::shared_ptr<T>(shPtr);
}

bool AtLeastOneEmptyString(const std::vector<std::string_view> strings);

/* The following functions upcast shared ptrs to TFilter, TCustomColumn, TRange to their parent class (***Base).
 * Shared ptrs to TLoopManager are just copied, as well as shared ptrs to ***Base classes. */
std::shared_ptr<TFilterBase> UpcastNode(const std::shared_ptr<TFilterBase> ptr);
std::shared_ptr<TCustomColumnBase> UpcastNode(const std::shared_ptr<TCustomColumnBase> ptr);
std::shared_ptr<TRangeBase> UpcastNode(const std::shared_ptr<TRangeBase> ptr);
std::shared_ptr<TLoopManager> UpcastNode(const std::shared_ptr<TLoopManager> ptr);

ColumnNames_t GetValidatedColumnNames(TLoopManager &lm, const unsigned int nColumns, const ColumnNames_t &columns,
                                      const ColumnNames_t &validCustomColumns, TDataSource *ds);

std::vector<bool> FindUndefinedDSColumns(const ColumnNames_t &requestedCols, const ColumnNames_t &definedDSCols);

/// Helper function to be used by `DefineDataSourceColumns`
template <typename T>
void DefineDSColumnHelper(std::string_view name, TLoopManager &lm, TDataSource &ds)
{
   auto readers = ds.GetColumnReaders<T>(name);
   auto getValue = [readers](unsigned int slot) { return *readers[slot]; };
   using NewCol_t = TCustomColumn<decltype(getValue), TCCHelperTypes::TSlot>;
   lm.Book(std::make_shared<NewCol_t>(name, std::move(getValue), ColumnNames_t{}, &lm, /*isDSColumn=*/true));
   lm.AddDataSourceColumn(name);
}

/// Take a list of data-source column names and define the ones that haven't been defined yet.
template <typename... ColumnTypes, int... S>
void DefineDataSourceColumns(const std::vector<std::string> &columns, TLoopManager &lm, StaticSeq<S...>,
                             TTraits::TypeList<ColumnTypes...>, TDataSource &ds)
{
   const auto mustBeDefined = FindUndefinedDSColumns(columns, lm.GetCustomColumnNames());
   if (std::none_of(mustBeDefined.begin(), mustBeDefined.end(), [](bool b) { return b; })) {
      // no need to define any column
      return;
   } else {
      // hack to expand a template parameter pack without c++17 fold expressions.
      std::initializer_list<int> expander{
         (mustBeDefined[S] ? DefineDSColumnHelper<ColumnTypes>(columns[S], lm, ds) : /*no-op*/ ((void)0), 0)...};
      (void)expander; // avoid unused variable warnings
   }
}

/// Convenience function invoked by jitted code to build action nodes at runtime
template <typename ActionType, typename... BranchTypes, typename PrevNodeType, typename ActionResultType>
void CallBuildAndBook(PrevNodeType &prevNode, const ColumnNames_t &bl, const unsigned int nSlots,
                      const std::shared_ptr<ActionResultType> *rOnHeap,
                      const std::shared_ptr<TActionBase *> *actionPtrPtrOnHeap)
{
   // if we are here it means we are jitting, if we are jitting the loop manager must be alive
   auto &loopManager = *prevNode.GetImplPtr();
   using ColTypes_t = TypeList<BranchTypes...>;
   constexpr auto nColumns = ColTypes_t::list_size;
   auto ds = loopManager.GetDataSource();
   if (ds)
      DefineDataSourceColumns(bl, loopManager, GenStaticSeq_t<nColumns>(), ColTypes_t(), *ds);
   TActionBase *actionPtr =
      BuildAndBook<BranchTypes...>(bl, *rOnHeap, nSlots, loopManager, prevNode, (ActionType *)nullptr);
   **actionPtrPtrOnHeap = actionPtr;
   delete rOnHeap;
   delete actionPtrPtrOnHeap;
}

/// The contained `type` alias is `double` if `T == TInferType`, `U` if `T == std::container<U>`, `T` otherwise.
template <typename T, bool Container = TTraits::IsContainer<T>::value>
struct TMinReturnType {
   using type = T;
};

template <>
struct TMinReturnType<TInferType, false> {
   using type = double;
};

template <typename T>
struct TMinReturnType<T, true> {
   using type = TTraits::TakeFirstParameter_t<T>;
};

// return wrapper around f that prepends an `unsigned int slot` parameter
template <typename R, typename F, typename... Args>
std::function<R(unsigned int, Args...)> AddSlotParameter(F &f, TypeList<Args...>)
{
   return [f](unsigned int, Args... a) -> R { return f(a...); };
}

template <typename BranchType, typename... Rest>
struct TNeedJitting {
   static constexpr bool value = TNeedJitting<Rest...>::value;
};

template <typename... Rest>
struct TNeedJitting<TInferType, Rest...> {
   static constexpr bool value = true;
};

template <typename T>
struct TNeedJitting<T> {
   static constexpr bool value = false;
};

template <>
struct TNeedJitting<TInferType> {
   static constexpr bool value = true;
};

ColumnNames_t GetBranchNames(TTree &t);

void CheckCustomColumn(std::string_view definedCol, TTree *treePtr, const ColumnNames_t &customCols,
                       const ColumnNames_t &dataSourceColumns);

///////////////////////////////////////////////////////////////////////////////
/// Check preconditions for TInterface::Aggregate:
/// - the aggregator callable must have signature `U(U,T)` or `void(U&,T)`.
/// - the merge callable must have signature `U(U,U)` or `void(std::vector<U>&)`
template <typename R, typename Merge, typename U, typename T, typename decayedU = typename std::decay<U>::type,
          typename mergeArgsNoDecay_t = typename CallableTraits<Merge>::arg_types_nodecay,
          typename mergeArgs_t = typename CallableTraits<Merge>::arg_types,
          typename mergeRet_t = typename CallableTraits<Merge>::ret_type>
void CheckAggregate(TypeList<U, T>)
{
   constexpr bool isAggregatorOk =
      (std::is_same<R, decayedU>::value) || (std::is_same<R, void>::value && std::is_lvalue_reference<U>::value);
   static_assert(isAggregatorOk, "aggregator function must have signature `U(U,T)` or `void(U&,T)`");
   constexpr bool isMergeOk =
      (std::is_same<TypeList<decayedU, decayedU>, mergeArgs_t>::value && std::is_same<decayedU, mergeRet_t>::value) ||
      (std::is_same<TypeList<std::vector<decayedU> &>, mergeArgsNoDecay_t>::value &&
       std::is_same<void, mergeRet_t>::value);
   static_assert(isMergeOk, "merge function must have signature `U(U,U)` or `void(std::vector<U>&)`");
}

///////////////////////////////////////////////////////////////////////////////
/// This overload of CheckAggregate is called when the aggregator takes more than two arguments
template <typename R, typename T>
void CheckAggregate(T)
{
   static_assert(sizeof(T) == 0, "aggregator function must take exactly two arguments");
}

///////////////////////////////////////////////////////////////////////////////
/// Check as many template parameters were passed as the number of column names, throw if this is not the case.
void CheckSnapshot(unsigned int nTemplateParams, unsigned int nColumnNames);

/// Return local BranchNames or default BranchNames according to which one should be used
const ColumnNames_t SelectColumns(unsigned int nArgs, const ColumnNames_t &bl, const ColumnNames_t &defBl);

/// Check whether column names refer to a valid branch of a TTree or have been `Define`d. Return invalid column names.
ColumnNames_t FindUnknownColumns(const ColumnNames_t &requiredCols, TTree *tree, const ColumnNames_t &definedCols,
                                 const ColumnNames_t &dataSourceColumns);

bool IsInternalColumn(std::string_view colName);

// Check if a condition is true for all types
template <bool...>
struct TBoolPack;

template <bool... bs>
using IsTrueForAllImpl_t = typename std::is_same<TBoolPack<bs..., true>, TBoolPack<true, bs...>>;

template <bool... Conditions>
struct TEvalAnd {
   static constexpr bool value = IsTrueForAllImpl_t<Conditions...>::value;
};

// Check if a class is a specialisation of stl containers templates
// clang-format off

template <typename>
struct IsList_t : std::false_type {};

template <typename T>
struct IsList_t<std::list<T>> : std::true_type {};

template <typename>
struct IsDeque_t : std::false_type {};

template <typename T>
struct IsDeque_t<std::deque<T>> : std::true_type {};
// clang-format on

} // namespace TDF
} // namespace Internal

namespace Detail {
namespace TDF {

/// The aliased type is `double` if `T == TInferType`, `U` if `T == container<U>`, `T` otherwise.
template <typename T>
using MinReturnType_t = typename TDFInternal::TMinReturnType<T>::type;

template <typename T>
using MaxReturnType_t = MinReturnType_t<T>;

template <typename T>
using SumReturnType_t = MinReturnType_t<T>;

template <typename T, typename COLL = std::vector<T>>
struct TTakeRealTypes {
   // We cannot put in the output collection C arrays: the ownership is not defined.
   // We therefore proceed to check if T is an TVec
   // If yes, we check what type is the output collection and we rebuild it.
   // E.g. if a vector<V> was the selected collection, where V is TVec<T>,
   // the collection becomes vector<vector<T>>.
   static constexpr auto isAB = TDFInternal::IsTVec_t<T>::value;
   using RealT_t = typename TDFInternal::ValueType<T>::value_type;
   using VTColl_t = std::vector<RealT_t>;

   using NewC0_t =
      typename std::conditional<isAB && TDFInternal::IsVector_t<COLL>::value, std::vector<VTColl_t>, COLL>::type;
   using NewC1_t =
      typename std::conditional<isAB && TDFInternal::IsList_t<NewC0_t>::value, std::list<VTColl_t>, NewC0_t>::type;
   using NewC2_t =
      typename std::conditional<isAB && TDFInternal::IsDeque_t<NewC1_t>::value, std::deque<VTColl_t>, NewC1_t>::type;
   using RealColl_t = NewC2_t;
};
template <typename T, typename C>
using ColType_t = typename TTakeRealTypes<T, C>::RealColl_t;
} // namespace TDF
} // namespace Detail
} // namespace ROOT

/// \endcond

#endif
