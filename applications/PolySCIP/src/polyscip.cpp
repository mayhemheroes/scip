/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*        This file is part of the program PolySCIP                          */
/*                                                                           */
/*    Copyright (C) 2012-2016 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  PolySCIP is distributed under the terms of the ZIB Academic License.     */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with PolySCIP; see the file LICENCE.                               */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "polyscip.h"

#include <algorithm> //std::transform, std::max
#include <array>
#include <cmath> //std::abs
#include <cstddef> //std::size_t
#include <fstream>
#include <functional> //std::plus
#include <iomanip> //std::set_precision
#include <iostream>
#include <iterator> //std::advance
#include <limits>
#include <ostream>
#include <memory> //std::addressof
#include <numeric> //std::inner_product
#include <stdexcept>
#include <string>
#include <vector>

#include "polytope_representation.h"
#include "scip/scip.h"
#include "objscip/objscipdefplugins.h"
#include "cmd_line_args.h"
#include "global_functions.h"
#include "polyscip_types.h"
#include "prob_data_objectives.h"
#include "ReaderMOP.h"
#include "weight_space_polyhedron.h"

using std::addressof;
using std::array;
using std::begin;
using std::cout;
using std::end;
using std::ostream;
using std::size_t;
using std::string;
using std::vector;

namespace polyscip {

    using DDMethod = polytoperepresentation::DoubleDescriptionMethod;

    Polyscip::Polyscip(int argc, const char *const *argv)
            : cmd_line_args_(argc, argv),
              polyscip_status_(PolyscipStatus::Unsolved),
              scip_(nullptr),
              obj_sense_(SCIP_OBJSENSE_MINIMIZE), // default objective sense is minimization
              clock_total_(nullptr)
    {
        if (cmd_line_args_.hasTimeLimit() && cmd_line_args_.getTimeLimit() <= 0)
            throw std::domain_error("Invalid time limit.");
        if (cmd_line_args_.hasParameterFile() && !filenameIsOkay(cmd_line_args_.getParameterFile()))
            throw std::invalid_argument("Invalid parameter settings file.");
        if (!filenameIsOkay(cmd_line_args_.getProblemFile()))
            throw std::invalid_argument("Invalid problem file.");

        SCIPcreate(&scip_);
        assert (scip_ != nullptr);
        SCIPincludeDefaultPlugins(scip_);
        SCIPincludeObjReader(scip_, new ReaderMOP(scip_), TRUE);
        SCIPcreateClock(scip_, addressof(clock_total_));
        if (cmd_line_args_.hasParameterFile())
            SCIPreadParams(scip_, cmd_line_args_.getParameterFile().c_str());
    }

    Polyscip::~Polyscip() {
        SCIPfreeClock(scip_, addressof(clock_total_));
        SCIPfree(addressof(scip_));
    }


    SCIP_RETCODE Polyscip::computeNondomPoints() {
        SCIP_CALL( SCIPstartClock(scip_, clock_total_) );
        SCIP_CALL( computeSupported() );
        if (cmd_line_args_.withUnsupported() && polyscip_status_ == PolyscipStatus::CompUnsupportedPhase) {
            SCIP_CALL( computeUnsupported() );
        }
        deleteWeaklyNondomResults();
        SCIP_CALL( SCIPstopClock(scip_, clock_total_) );
        return SCIP_OKAY;
    }

    SCIP_RETCODE Polyscip::computeUnitWeightOutcomes() {
        polyscip_status_ = PolyscipStatus::InitPhase;
        auto weight = WeightType(considered_objs_.size(),0.);
        for (auto unit_weight_index : considered_objs_) {
            if (polyscip_status_ != PolyscipStatus::InitPhase)
                break;
            weight[unit_weight_index] = 1.;
            SCIP_CALL(setWeightedObjective(weight));
            SCIP_CALL(solve());
            auto scip_status = SCIPgetStatus(scip_);
            if (scip_status == SCIP_STATUS_INFORUNBD)
                scip_status = separateINFORUNBD(weight);

            if (scip_status == SCIP_STATUS_OPTIMAL) {
                SCIP_CALL( handleOptimalStatus(true) );
            }
            else if (scip_status == SCIP_STATUS_UNBOUNDED) {
                SCIP_CALL( handleUnboundedStatus(true) );
            }
            else {
                SCIP_CALL( handleNonOptNonUnbdStatus(scip_status) );
            }

            weight[unit_weight_index] = 0.;
        }
        return SCIP_OKAY;
    }

    SCIP_RETCODE Polyscip::computeUnsupported() {
        auto constraint_pairs = weight_space_poly_->getConstraintsForUnsupported();
        for (const auto& item : constraint_pairs) {
            if (item.first != item.second)
                SCIP_CALL( computeUnsupported(scip_, item.first, item.second) );
        }
        return SCIP_OKAY;
    }

    SCIP_RETCODE Polyscip::computeUnsupported(SCIP* scip, const OutcomeType& upper_bound, const OutcomeType& ref_point) {
        std::cout << "upper_bound: ";
        global::print(upper_bound);
        std::cout << " ref_point: ";
        global::print(ref_point, "", "\n");
        return SCIP_OKAY;
    }

    SCIP_RETCODE Polyscip::initWeightSpace() {
        SCIP_CALL( computeUnitWeightOutcomes() ); // computes optimal outcomes for all unit weights
        if (polyscip_status_ == PolyscipStatus::InitPhase) {
            if (supported_.empty()) {
                polyscip_status_ = PolyscipStatus::Finished; // all outcomes for unit weights are unbounded
            }
            else {
                auto v_rep = DDMethod(scip_, supported_, unbounded_);
                v_rep.computeVRep();
                std::cout << "SIZE OF VREP = " << v_rep.size() << "\n";
                std::cout << "Starting initializing WSP...";
                weight_space_poly_ = global::make_unique<WeightSpacePolyhedron>(scip_,
                                                                                considered_objs_.size(),
                                                                                v_rep.moveVRep(),
                                                                                v_rep.moveHRep());
                std::cout << "...finished.\n";
                polyscip_status_ = PolyscipStatus::WeightSpacePhase;
            }
        }
        return SCIP_OKAY;
    }



    SCIP_STATUS Polyscip::separateINFORUNBD(const WeightType& weight, bool with_presolving) {
        if (!with_presolving)
            SCIPsetPresolving(scip_, SCIP_PARAMSETTING_OFF, TRUE);
        auto zero_weight = WeightType(considered_objs_.size(), 0.);
        setWeightedObjective(zero_weight);
        solve(); // re-compute with zero objective
        if (!with_presolving)
            SCIPsetPresolving(scip_, SCIP_PARAMSETTING_DEFAULT, TRUE);
        auto status = SCIPgetStatus(scip_);
        setWeightedObjective(weight); // re-set to previous objective
        if (status == SCIP_STATUS_INFORUNBD) {
            if (with_presolving)
                separateINFORUNBD(weight, false);
            else
                throw std::runtime_error("INFORUNBD Status for problem with zero objective and no presolving.\n");
        }
        else if (status == SCIP_STATUS_UNBOUNDED) {
            throw std::runtime_error("UNBOUNDED Status for problem with zero objective.\n");
        }
        else if (status == SCIP_STATUS_OPTIMAL) { // previous problem was unbounded
            return SCIP_STATUS_UNBOUNDED;
        }
        return status;
    }


    SCIP_RETCODE Polyscip::handleNonOptNonUnbdStatus(SCIP_STATUS status) {
        assert (status != SCIP_STATUS_OPTIMAL && status != SCIP_STATUS_UNBOUNDED);
        if (status == SCIP_STATUS_INFORUNBD) {
            throw std::runtime_error("INFORUNBD Status unexpected at this stage.\n");
        }
        else if (status == SCIP_STATUS_TIMELIMIT) {
            polyscip_status_ = PolyscipStatus::TimeLimitReached;
        }
        else {
            polyscip_status_ = PolyscipStatus::Finished;
        }
        return SCIP_OKAY;
    }

    SCIP_RETCODE Polyscip::handleUnboundedStatus(bool check_if_new_result) {
        if (!SCIPhasPrimalRay(scip_)) {
            SCIP_CALL( SCIPsetPresolving(scip_, SCIP_PARAMSETTING_OFF, TRUE) );
            if (SCIPisTransformed(scip_))
                SCIP_CALL( SCIPfreeTransform(scip_) );
            SCIP_CALL( solve() );
            SCIP_CALL( SCIPsetPresolving(scip_, SCIP_PARAMSETTING_DEFAULT, TRUE) );
            if (SCIPgetStatus(scip_) != SCIP_STATUS_UNBOUNDED)
                throw std::runtime_error("Status UNBOUNDED expected.\n");
            if (!SCIPhasPrimalRay(scip_))
                throw std::runtime_error("Existence of primal ray expected.\n");
        }
        addResult(check_if_new_result);
        return SCIP_OKAY;
    }

    SCIP_RETCODE Polyscip::handleOptimalStatus(bool check_if_new_result) {
        std::cout << "handleOptimalStatus\n";
        auto best_sol = SCIPgetBestSol(scip_);
        std::cout << "got best sol\n";
        SCIP_SOL *finite_sol{nullptr};
        SCIP_Bool same_obj_val{FALSE};
        std::cout << "scipcreateFiniteSolCopy\n";
        SCIP_CALL(SCIPcreateFiniteSolCopy(scip_, addressof(finite_sol), best_sol, addressof(same_obj_val)));

        if (!same_obj_val) {
            auto diff = std::abs(SCIPgetSolOrigObj(scip_, best_sol) -
                                 SCIPgetSolOrigObj(scip_, finite_sol));
            if (diff > 1.0e-5) {
                std::cerr << "absolute value difference after calling SCIPcreateFiniteSolCopy: " << diff << "\n";
                SCIP_CALL(SCIPfreeSol(scip_, addressof(finite_sol)));
                throw std::runtime_error("SCIPcreateFiniteSolCopy: unacceptable difference in objective values.");
            }
        }
        assert (finite_sol != nullptr);
        std::cout << "entering addResult\n";
        addResult(check_if_new_result, true, finite_sol);
        SCIP_CALL(SCIPfreeSol(scip_, addressof(finite_sol)));
        return SCIP_OKAY;
    }

    void Polyscip::addResult(bool check_if_new_result, bool outcome_is_bounded, SCIP_SOL* primal_sol) {
        SolType sol;
        auto outcome = OutcomeType(considered_objs_.size(),0.);
        auto no_vars = SCIPgetNOrigVars(scip_);
        auto vars = SCIPgetOrigVars(scip_);
        auto objs_probdata = dynamic_cast<ProbDataObjectives*>(SCIPgetObjProbData(scip_));
        for (auto i=0; i<no_vars; ++i) {
            auto var_sol_val = outcome_is_bounded ? SCIPgetSolVal(scip_, primal_sol, vars[i]) :
                               SCIPgetPrimalRayVal(scip_, vars[i]);

            if (!SCIPisZero(scip_, var_sol_val)) {
                sol.emplace_back(SCIPvarGetName(vars[i]), var_sol_val);
                auto var_obj_vals = OutcomeType(considered_objs_.size(), 0.);
                //for (decltype(no_objs_) j=0; j<no_objs_; ++j)
                for (auto index : considered_objs_)
                    var_obj_vals[index] = objs_probdata->getObjVal(vars[i], index, var_sol_val);
                std::transform(begin(outcome), end(outcome),
                               begin(var_obj_vals),
                               begin(outcome),
                               std::plus<ValueType>());

            }

        }

        if (!check_if_new_result || outcomeIsNew(outcome, outcome_is_bounded)) {
            if (outcome_is_bounded)
                supported_.push_back({sol, outcome});
            else
                unbounded_.push_back({sol, outcome});
        }
        else {
            global::print(outcome, "Outcome: [", "]");
            cout << "not added to results.\n";
        }
    }

    bool Polyscip::outcomeIsNew(const OutcomeType& outcome, bool outcome_is_bounded) const {
        auto beg_it = outcome_is_bounded ? begin(supported_) : begin(unbounded_);
        auto end_it = outcome_is_bounded ? end(supported_) : end(unbounded_);
        return std::find_if(beg_it, end_it, [&outcome](const Result& res){return outcome == res.second;}) == end_it;
    }

    SCIP_RETCODE Polyscip::solve() {
        if (cmd_line_args_.hasTimeLimit()) { // set SCIP timelimit
            auto remaining_time = std::max(cmd_line_args_.getTimeLimit() -
                                           SCIPgetClockTime(scip_, clock_total_), 0.);
            SCIP_CALL(SCIPsetRealParam(scip_, "limits/time", remaining_time));
        }
        SCIP_CALL( SCIPsolve(scip_) );    // actual SCIP solver call
        return SCIP_OKAY;
    }

    SCIP_RETCODE Polyscip::setWeightedObjective(const WeightType& weight){
        if (SCIPisTransformed(scip_))
            SCIP_CALL( SCIPfreeTransform(scip_) );
        auto obj_probdata = dynamic_cast<ProbDataObjectives*>(SCIPgetObjProbData(scip_));
        assert (obj_probdata != nullptr);
        auto vars = SCIPgetOrigVars(scip_);
        auto no_vars = SCIPgetNOrigVars(scip_);
        for (auto i=0; i<no_vars; ++i) {
            auto val = obj_probdata->getWeightedObjVal(vars[i], weight, considered_objs_);
            SCIP_CALL( SCIPchgVarObj(scip_, vars[i], val) );
        }
        return SCIP_OKAY;
    }

    SCIP_RETCODE Polyscip::computeSupported() {
        SCIP_CALL( initWeightSpace() );
        if (polyscip_status_ == PolyscipStatus::WeightSpacePhase) {
            std::cout << "Starting weight space phase...\n";
            while (weight_space_poly_->hasUntestedWeight()) {
                auto untested_weight = weight_space_poly_->getUntestedWeight();
                std::cout << "new weight: ";
                global::print(untested_weight);
                SCIP_CALL( setWeightedObjective(untested_weight) );
                std::cout << "solving...";
                SCIP_CALL( solve() );
                std::cout << "...finished\n";
                auto scip_status = SCIPgetStatus(scip_);
                std::cout << "STATUS = " << scip_status << "\n";
                if (scip_status == SCIP_STATUS_INFORUNBD)
                    scip_status = separateINFORUNBD(untested_weight);
                if (scip_status == SCIP_STATUS_OPTIMAL) {
                    if (SCIPgetPrimalbound(scip_)+cmd_line_args_.getEpsilon() < weight_space_poly_->getUntestedVertexWOV(untested_weight)) {
                        SCIP_CALL( handleOptimalStatus() ); //adds bounded result to supported_
                        std::cout << "incorporating new outcome...";
                        weight_space_poly_->incorporateNewOutcome(cmd_line_args_.getEpsilon(),
                                                                  untested_weight,
                                                                  supported_.back().second); // was added by handleOptimalStatus()
                        std::cout << "...finished.\n";
                    }
                    else {
                        std::cout << "incorporating old outcome...";
                        weight_space_poly_->incorporateKnownOutcome(untested_weight);
                        std::cout << "...finished.\n";
                    }
                }
                else if (scip_status == SCIP_STATUS_UNBOUNDED) {
                    SCIP_CALL( handleUnboundedStatus() ); //adds unbounded result to unbounded_
                    std::cout << "incorporating unbounded outcome...";
                    weight_space_poly_->incorporateNewOutcome(cmd_line_args_.getEpsilon(),
                                                              untested_weight,
                                                              unbounded_.back().second, // was added by handleUnboundedStatus()
                                                              true);
                    std::cout << "...finished.\n";
                }
                else {
                    SCIP_CALL( handleNonOptNonUnbdStatus(scip_status) ); //polyscip_status_ is set to finished or time limit reached
                    return SCIP_OKAY;
                }
            }
            std::cout << "...finished.\n";
            //weight_space_poly_->printMarkedVertices(std::cout, true);
            if (SCIPgetNOrigContVars(scip_) == SCIPgetNOrigVars(scip_)) {   //check whether there exists integer variables
                polyscip_status_ = PolyscipStatus::Finished;
            }
            else {
                polyscip_status_ = PolyscipStatus::CompUnsupportedPhase;
            }
        }
        return SCIP_OKAY;
    }

    void Polyscip::printSupportedResults(ostream& os, bool withSolutions) const {
        os << "Number of supported bounded results: " << supported_.size() << "\n";
        os << "Number of supported unbounded results: " << unbounded_.size() << "\n";
        for (const auto& result : supported_) {
            printPoint(result.second, os);
            if (withSolutions)
                printSol(result.first, os);
            os << "\n";
        }
        for (const auto& result : unbounded_) {
            printRay(result.second, os);
            if (withSolutions)
                printSol(result.first, os);
            os << "\n";
        }
    }

    void Polyscip::printSol(const SolType& sol, ostream& os) const {
        os << " Non-zero solution variables: ";
        for (const auto& elem : sol)
            os << elem.first << "=" << elem.second << " ";
    }

    void Polyscip::printPoint(const OutcomeType& point, ostream& os) const {
        global::print(point, "Point = [", "]", os);
    }

    void Polyscip::printRay(const OutcomeType& ray, ostream& os) const {
        global::print(ray, "Ray = [", "]", os);
    }

    bool Polyscip::filenameIsOkay(const string& filename) {
        std::ifstream file(filename.c_str());
        return file.good();
    }

    /*void Polyscip::computeNonRedundantObjectives(bool printObjectives) {
        auto obj_probdata = dynamic_cast<ProbDataObjectives*>(SCIPgetObjProbData(scip_));
        assert (obj_probdata != nullptr);
        auto vars = SCIPgetOrigVars(scip_);
        auto no_objs = obj_probdata->getNoAllObjs();
        auto begin_nonzeros = vector<int>(no_objs,0);
        for (size_t i = 0; i<no_objs-1; ++i)
            begin_nonzeros[i+1] = global::narrow_cast<int>(begin_nonzeros[i] + obj_probdata->getNumberNonzeroCoeffs(i));

        auto obj_to_nonzero_inds = vector< vector<int> >{};
        auto obj_to_nonzero_vals = vector< vector<SCIP_Real> >{};
        for (size_t obj_index=0; obj_index<no_objs; ++obj_index) {
            auto nonzero_vars = obj_probdata->getNonZeroCoeffVars(obj_index);
            auto size = nonzero_vars.size();
            assert (size > 0);
            auto nonzero_inds = vector<int>(size, 0);
            std::transform(begin(nonzero_vars),
                           end(nonzero_vars),
                           begin(nonzero_inds),
                           [](SCIP_VAR* var){return SCIPvarGetProbindex(var);});
            std::sort(begin(nonzero_inds), end(nonzero_inds));

            auto nonzero_vals = vector<SCIP_Real>(size, 0.);
            std::transform(begin(nonzero_inds),
                           end(nonzero_inds),
                           begin(nonzero_vals),
                           [&](int var_ind){return obj_probdata->getObjCoeff(vars[var_ind], obj_index);});

            if (printObjectives)
                printObjective(obj_index, nonzero_inds, nonzero_vals);

            obj_to_nonzero_inds.push_back(std::move(nonzero_inds));
            obj_to_nonzero_vals.push_back(std::move(nonzero_vals));
        }

        considered_objs_.push_back(0);
        for (size_t obj_no=1; obj_no<no_objs; ++obj_no) {
            if (!objIsRedundant(begin_nonzeros,
                               obj_to_nonzero_inds,
                               obj_to_nonzero_vals,
                               obj_no))
                considered_objs_.push_back(obj_no);
            else
                std::cout << "objective no: " << obj_no << " is redundant.\n";
        }
    }*/

    void Polyscip::printObjective(size_t obj_no,
                                  const std::vector<int>& nonzero_indices,
                                  const std::vector<SCIP_Real>& nonzero_vals,
                                  ostream& os) const {
        assert (!nonzero_indices.empty());
        auto size = nonzero_indices.size();
        assert (size == nonzero_vals.size());
        auto obj = vector<SCIP_Real>(global::narrow_cast<size_t>(SCIPgetNOrigVars(scip_)), 0);
        for (size_t i=0; i<size; ++i)
            obj[nonzero_indices[i]] = nonzero_vals[i];
        global::print(obj, std::to_string(obj_no) + ". obj: [", "]", os);
        os << "\n";
    }

    bool Polyscip::objIsRedundant(const vector<int>& begin_nonzeros,
                                  const vector< vector<int> >& obj_to_nonzero_indices,
                                  const vector< vector<SCIP_Real> >& obj_to_nonzero_values,
                                  size_t checked_obj) const {
        bool is_redundant = false;
        auto obj_probdata = dynamic_cast<ProbDataObjectives*>(SCIPgetObjProbData(scip_));

        assert (obj_probdata != nullptr);
        assert (checked_obj >= 1 && checked_obj < obj_probdata->getNoAllObjs());

        SCIP_LPI* lpi;
        auto retcode = SCIPlpiCreate(addressof(lpi), nullptr, "check objective redundancy", SCIP_OBJSEN_MINIMIZE);
        if (retcode != SCIP_OKAY)
            throw std::runtime_error("no SCIP_OKAY for SCIPlpiCreate\n.");

        auto no_cols = global::narrow_cast<int>(checked_obj);
        auto obj = vector<SCIP_Real>(checked_obj, 1.);
        auto lb = vector<SCIP_Real>(checked_obj, 0.);
        auto ub = vector<SCIP_Real>(checked_obj, SCIPinfinity(scip_));
        auto no_nonzero = begin_nonzeros.at(checked_obj);

        auto beg = vector<int>(begin(begin_nonzeros), begin(begin_nonzeros)+checked_obj);
        auto ind = vector<int>{};
        ind.reserve(global::narrow_cast<size_t>(no_nonzero));
        auto val = vector<SCIP_Real>{};
        val.reserve(global::narrow_cast<size_t>(no_nonzero));
        for (size_t i=0; i<checked_obj; ++i) {
            ind.insert(end(ind), begin(obj_to_nonzero_indices[i]), end(obj_to_nonzero_indices[i]));
            val.insert(end(val), begin(obj_to_nonzero_values[i]), end(obj_to_nonzero_values[i]));
        }

        auto no_rows = SCIPgetNOrigVars(scip_);
        auto vars = SCIPgetOrigVars(scip_);
        auto lhs = vector<SCIP_Real>(global::narrow_cast<size_t>(no_rows), 0.);
        for (auto i=0; i<no_rows; ++i)
            lhs[i] = obj_probdata->getObjCoeff(vars[i], checked_obj);
        auto rhs = vector<SCIP_Real>(lhs);

        retcode =  SCIPlpiLoadColLP(lpi,
                                    SCIP_OBJSEN_MINIMIZE,
                                    no_cols,
                                    obj.data(),
                                    lb.data(),
                                    ub.data(),
                                    nullptr,
                                    no_rows,
                                    lhs.data(),
                                    rhs.data(),
                                    nullptr,
                                    no_nonzero,
                                    beg.data(),
                                    ind.data(),
                                    val.data());

        if (retcode != SCIP_OKAY)
            throw std::runtime_error("no SCIP_OKAY for SCIPlpiLoadColLP\n");

        retcode = SCIPlpiSolvePrimal(lpi);
        if (retcode != SCIP_OKAY)
            throw std::runtime_error("no SCIP_OKAY for SCIPlpiSolvePrimal\n");

        if (SCIPlpiIsPrimalFeasible(lpi)) {
            is_redundant = true;
        }
        else {
            assert (SCIPlpiIsPrimalInfeasible(lpi));
        }

        retcode = SCIPlpiFree(addressof(lpi));
        if (retcode != SCIP_OKAY)
            throw std::runtime_error("no SCIP_OKAY for SCIPlpiFree\n");

        return is_redundant;
    }

    SCIP_RETCODE Polyscip::readProblem() {
        auto filename = cmd_line_args_.getProblemFile();
        SCIP_CALL( SCIPreadProb(scip_, filename.c_str(), "mop") );
        auto obj_probdata = dynamic_cast<ProbDataObjectives*>(SCIPgetObjProbData(scip_));
        assert (obj_probdata != nullptr);
        no_all_objs_ = obj_probdata->getNoAllObjs();

        if (cmd_line_args_.beVerbose() || cmd_line_args_.checkForRedundantObjs()) {
            auto vars = SCIPgetOrigVars(scip_);
            auto begin_nonzeros = vector<int>(no_all_objs_, 0);
            for (size_t i = 0; i<no_all_objs_-1; ++i)
                begin_nonzeros[i+1] = global::narrow_cast<int>(begin_nonzeros[i] + obj_probdata->getNumberNonzeroCoeffs(i));

            auto obj_to_nonzero_inds = vector< vector<int> >{};
            auto obj_to_nonzero_vals = vector< vector<SCIP_Real> >{};
            for (size_t obj_ind=0; obj_ind<no_all_objs_; ++obj_ind) {
                auto nonzero_vars = obj_probdata->getNonZeroCoeffVars(obj_ind);
                auto size = nonzero_vars.size();
                if (size == 0)
                    throw std::runtime_error(std::to_string(obj_ind) + ". objective is zero objective!");
                auto nonzero_inds = vector<int>(size, 0);
                std::transform(begin(nonzero_vars),
                               end(nonzero_vars),
                               begin(nonzero_inds),
                               [](SCIP_VAR* var){return SCIPvarGetProbindex(var);});
                std::sort(begin(nonzero_inds), end(nonzero_inds));

                auto nonzero_vals = vector<SCIP_Real>(size, 0.);
                std::transform(begin(nonzero_inds),
                               end(nonzero_inds),
                               begin(nonzero_vals),
                               [&](int var_ind){return obj_probdata->getObjCoeff(vars[var_ind], obj_ind);});

                if (cmd_line_args_.beVerbose())
                    printObjective(obj_ind, nonzero_inds, nonzero_vals);

                if (cmd_line_args_.checkForRedundantObjs()) {
                    obj_to_nonzero_inds.push_back(std::move(nonzero_inds));
                    obj_to_nonzero_vals.push_back(std::move(nonzero_vals));
                }
                else {
                    considered_objs_.push_back(obj_ind);
                }
            }

            if (cmd_line_args_.checkForRedundantObjs()) {
                considered_objs_.push_back(0);
                for (size_t obj_no=1; obj_no<no_all_objs_; ++obj_no) {
                    if (!objIsRedundant(begin_nonzeros,
                                        obj_to_nonzero_inds,
                                        obj_to_nonzero_vals,
                                        obj_no))
                        considered_objs_.push_back(obj_no);
                    else
                        cout << "Objective no: " << obj_no << " is redundant.\n";
                }
            }
        }
        else {
            for (size_t obj_ind=0; obj_ind<no_all_objs_; ++obj_ind)
                considered_objs_.push_back(obj_ind);
        }

        if (SCIPgetObjsense(scip_) == SCIP_OBJSENSE_MAXIMIZE) {
            obj_sense_ = SCIP_OBJSENSE_MAXIMIZE;
            // internally we treat problem as min problem and negate objective coefficients
            SCIPsetObjsense(scip_, SCIP_OBJSENSE_MINIMIZE);
            obj_probdata->negateAllCoeffs();
        }
        if (cmd_line_args_.beVerbose()) {
            cout << "Objective sense: ";
            if (obj_sense_ == SCIP_OBJSENSE_MAXIMIZE)
                cout << "MAXIMIZE\n";
            else
                cout << "MINIMIZE\n";
            cout << "Number of considered objectives: " << considered_objs_.size() << "\n";
        }
        return SCIP_OKAY;
    }

    void Polyscip::writeFileForVertexEnumeration() const {
        auto prob_file = cmd_line_args_.getProblemFile();
        size_t prefix = prob_file.find_last_of("/"), //separate path/ and filename.mop
                suffix = prob_file.find_last_of("."),      //separate filename and .mop
                start_ind = (prefix == string::npos) ? 0 : prefix + 1,
                end_ind = (suffix != string::npos) ? suffix : string::npos;
        string file_name = prob_file.substr(start_ind, end_ind - start_ind) + ".ine";
        std::ofstream solfs(file_name);
        if (solfs.is_open()) {
            solfs << "WeightSpacePolyhedron\n";
            solfs << "H-representation\n";
            solfs << "begin\n";
            solfs << supported_.size() + unbounded_.size() + no_all_objs_ << " " << considered_objs_.size() + 1 << " rational\n";
            for (const auto& elem : supported_) {
                global::print(elem.second, "0 ", " -1\n", solfs);
            }
            for (const auto& elem : unbounded_) {
                global::print(elem.second, "0 ", " 0", solfs);
            }
            for (size_t i=0; i<no_all_objs_; ++i) {
                auto ineq = vector<unsigned>(no_all_objs_, 0);
                ineq[i] = 1;
                global::print(ineq, "0 ", " 0\n", solfs);
            }
            solfs << "end\n";
            solfs.close();
        }
        else
            cout << "ERROR writing vertex enumeration file\n.";
    }

    void Polyscip::writeSupportedResults() const {
        auto prob_file = cmd_line_args_.getProblemFile();
        size_t prefix = prob_file.find_last_of("/"), //separate path/ and filename.mop
                suffix = prob_file.find_last_of("."),      //separate filename and .mop
                start_ind = (prefix == string::npos) ? 0 : prefix + 1,
                end_ind = (suffix != string::npos) ? suffix : string::npos;
        string file_name = "solutions_" +
                           prob_file.substr(start_ind, end_ind - start_ind) + ".txt";
        auto write_path = cmd_line_args_.getWritePath();
        if (write_path.back() != '/')
            write_path.push_back('/');
        std::ofstream solfs(write_path + file_name);
        if (solfs.is_open()) {
            printSupportedResults(solfs);
            solfs.close();
            cout << "#Solution file " << file_name
            << " written to: " << write_path << "\n";
        }
        else
            cout << "ERROR writing solution file\n.";
    }

    bool Polyscip::isDominatedOrEqual(ResultContainer::const_iterator it) const {
        for (auto curr = supported_.cbegin(); curr != supported_.cend(); ++curr) {
            if (it == curr)
                continue;
            else if (std::equal(begin(curr->second),
                                end(curr->second),
                                begin(it->second),
                                std::less_equal<ValueType>()))
                return true;
        }
        return false;
    }


    void Polyscip::deleteWeaklyNondomResults() {
        auto it = begin(supported_);
        while (it != end(supported_)) {
            if (isDominatedOrEqual(it)) {
                cout << "Deleting weakly non-dominated point.\n";
                it = supported_.erase(it);
            }
            else {
                ++it;
            }
        }
    }

}
