/*---------------------------------------------------------------------------*\
License
    This file is part of OGL.

    OGL is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.


Author: Gregor Olenik <go@hpsim.de>

SourceFiles
    common.H

\*---------------------------------------------------------------------------*/
#ifndef OGL_COMMON_H
#define OGL_COMMON_H

#include "fvCFD.H"
#include "regIOobject.H"

#include "../IOExecutorHandler/IOExecutorHandler.H"
#include "../IOGKOMatrixHandler/IOGKOMatrixHandler.H"
#include "../IOHandler/IOPreconditioner/IOPreconditioner.H"
#include "../IOHandler/IOSortingIdxHandler/IOSortingIdxHandler.H"
#include "../common/StoppingCriterion.H"

#include <ginkgo/ginkgo.hpp>
#include <map>

#define SIMPLE_LOG(VERBOSE, MSG)                       \
    if (VERBOSE) {                                     \
        std::cout << "[OGL LOG] " << MSG << std::endl; \
    }

#define SIMPLE_TIME(VERBOSE, NAME, F)                                       \
    auto start_##NAME = std::chrono::steady_clock::now();                   \
    F auto end_##NAME = std::chrono::steady_clock::now();                   \
    if (VERBOSE) {                                                          \
        std::cout << "[OGL LOG] " #NAME " : "                               \
                  << std::chrono::duration_cast<std::chrono::milliseconds>( \
                         end_##NAME - start_##NAME)                         \
                         .count()                                           \
                  << " ms\n";                                               \
    }


namespace Foam {

template <class MatrixType>
class HostMatrix : public MatrixType::solver {
private:
    const label nCells_;

    const label nNeighbours_;

    const label nElems_;

    mutable std::vector<scalar> values_;

    mutable std::vector<label> col_idxs_;

    mutable std::vector<label> row_idxs_;

public:
    HostMatrix(const word &fieldName, const MatrixType &matrix,
               const FieldField<Field, scalar> &interfaceBouCoeffs,
               const FieldField<Field, scalar> &interfaceIntCoeffs,
               const lduInterfaceFieldPtrsList &interfaces,
               const dictionary &solverControls)
        : MatrixType::solver(fieldName, matrix, interfaceBouCoeffs,
                             interfaceIntCoeffs, interfaces, solverControls),
          nCells_(matrix.diag().size()),
          nNeighbours_(matrix.lduAddr().upperAddr().size()),
          nElems_(nCells_ + 2 * nNeighbours_){};

    HostMatrix(const word &fieldName, const MatrixType &matrix,
               const dictionary &solverControls)
        : MatrixType::solver(fieldName, matrix, solverControls),
          nCells_(matrix.diag().size()),
          nNeighbours_(matrix.lduAddr().upperAddr().size()),
          nElems_(nCells_ + 2 * nNeighbours_){};

    void init_host_sparsity_pattern() const
    {
        col_idxs_.resize(0);
        row_idxs_.resize(0);

        col_idxs_.reserve(nElems());
        row_idxs_.reserve(nElems());

        // fill vectors
        // unsorted
        for (label i = 0; i < nNeighbours(); ++i) {
            row_idxs_.push_back(this->matrix().lduAddr().lowerAddr()[i]);
            col_idxs_.push_back(this->matrix().lduAddr().upperAddr()[i]);
        }

        for (label i = 0; i < nCells(); ++i) {
            col_idxs_.push_back(i);
            row_idxs_.push_back(i);
        }

        for (label i = 0; i < nNeighbours(); ++i) {
            row_idxs_.push_back(this->matrix().lduAddr().upperAddr()[i]);
            col_idxs_.push_back(this->matrix().lduAddr().lowerAddr()[i]);
        }
    }

    void sort_host_matrix_sparsity_pattern(
        const IOField<label> *sorting_idxs) const
    {
        std::vector<label> tmp_col_idxs(nElems());
        std::vector<label> tmp_row_idxs(nElems());

        for (label i = 0; i < nElems(); i++) tmp_col_idxs[i] = col_idxs_[i];
        for (label i = 0; i < nElems(); i++) tmp_row_idxs[i] = row_idxs_[i];

        for (label i = 0; i < nElems(); i++) {
            label j = sorting_idxs->operator[](i);
            col_idxs_[i] = tmp_col_idxs[j];
            row_idxs_[i] = tmp_row_idxs[j];
        }
    };


    void update_host_matrix_data() const
    {
        // reset
        // vectors
        values_.resize(0);

        values_.reserve(nElems());

        // fill vectors
        // unsorted
        for (label i = 0; i < nNeighbours(); ++i) {
            values_.push_back(this->matrix().lower()[i]);
        }

        for (label i = 0; i < nCells(); ++i) {
            values_.push_back(this->matrix().diag()[i]);
        }

        for (label i = 0; i < nNeighbours(); ++i) {
            values_.push_back(this->matrix().upper()[i]);
        }
    };

    void sort_host_matrix_data(const IOField<label> *sorting_idxs) const
    {
        std::vector<scalar> tmp_values(nElems());

        for (label i = 0; i < nElems(); i++) tmp_values[i] = values_[i];

        for (label i = 0; i < nElems(); i++) {
            label j = sorting_idxs->operator[](i);
            values_[i] = tmp_values[j];
        }
    };

    label nCells() const { return nCells_; };

    label nElems() const { return nElems_; };

    label nNeighbours() const { return nNeighbours_; };

    std::vector<scalar> &values() const { return values_; };

    std::vector<label> &col_idxs() const { return col_idxs_; };

    std::vector<label> &row_idxs() const { return row_idxs_; };
};

void export_system(const word fieldName, const mtx *A, const vec *x,
                   const vec *b, const word time);


// Logs the number of
// iteration executed
struct IterationLogger : gko::log::Logger {
    void on_iteration_complete(const gko::LinOp *,
                               const gko::size_type &num_iterations,
                               const gko::LinOp *residual,
                               const gko::LinOp *res_norm,
                               const gko::LinOp *) const override
    {
        this->num_iters = num_iterations;
    }

    IterationLogger(std::shared_ptr<const gko::Executor> exec)
        : exec(exec),
          gko::log::Logger(exec, gko::log::Logger::iteration_complete_mask)
    {}

    gko::size_type get_iters() { return num_iters; }

private:
    std::shared_ptr<const gko::Executor> exec;
    mutable gko::size_type num_iters{0};
};


}  // namespace Foam

#endif
