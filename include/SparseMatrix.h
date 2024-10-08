/**
 * @file SparseMatrix.h
 * @brief Sparse matrix in CRS format with optimized row-wise iteration.
 *
 * This file defines a `SparseMatrix` struct that represents a sparse matrix in Compressed Row Storage (CRS) format.
 * It supports efficient row-wise access to non-zero elements and can convert the sparse matrix to a dense format.
 *
 * The struct uses an optimized build function to convert a list of sparse elements into CRS format, enabling
 * fast row-wise traversal using an iterator.
 */

#pragma once

#include <algorithm>
#include <numeric> // for std::partial_sum
#include <vector>

/**
 * @struct SparseElement
 * @brief Represents a non-zero element in a sparse matrix.
 *
 * Each `SparseElement` stores the row and column indices, as well as the value of a matrix element.
 */
struct SparseElement {
    int    row;
    int    col;
    double value;
};

/**
 * @struct SparseMatrix
 * @brief A sparse matrix in Compressed Row Storage (CRS) format.
 *
 * The sparse matrix stores non-zero elements and allows efficient row-wise access through iterators.
 * The `SparseMatrix` struct also provides methods to insert elements, build the CRS structure,
 * and convert the matrix to a dense representation.
 */
struct SparseMatrix {
    std::vector<SparseElement> elements;  ///< Non-zero elements of the matrix
    std::vector<int>           row_start; ///< Starting index of each row in `elements`
    int                        num_rows;  ///< Total number of rows in the matrix
    int                        num_cols;  ///< Total number of columns in the matrix

    /**
     * @brief Insert a non-zero element into the sparse matrix.
     *
     * This method does not guarantee matrix consistency until `buildRowStart()` is called.
     *
     * @param row The row index of the element.
     * @param col The column index of the element.
     * @param value The value of the element.
     */
    void insert(int row, int col, double value) { elements.push_back({row, col, value}); }

    /**
     * @brief Build the CRS structure by computing row start positions.
     *
     * This function sorts the non-zero elements by row and column and then computes the starting index for
     * each row in the `elements` vector. This enables efficient row-wise traversal.
     */
    void buildRowStart() {
        // Initialize row_start to size num_rows + 1, filled with zeros
        row_start.assign(num_rows + 1, 0);

        // Sort elements by row first, then by column within the same row
        std::sort(elements.begin(), elements.end(), [](const SparseElement &a, const SparseElement &b) {
            return (a.row < b.row) || (a.row == b.row && a.col < b.col);
        });

        // Count occurrences of non-zero elements in each row
        for (const auto &el : elements) { ++row_start[el.row + 1]; }

        // Compute row_start by accumulating the counts to get the starting index for each row
        std::partial_sum(row_start.begin(), row_start.end(), row_start.begin());
    }

    /**
     * @struct RowIterator
     * @brief Iterator for efficiently traversing non-zero elements of a specific row.
     */
    struct RowIterator {
        const SparseMatrix &matrix;
        size_t              index;
        size_t              end;

        /**
         * @brief Constructor for `RowIterator`.
         *
         * @param matrix Reference to the `SparseMatrix` being traversed.
         * @param row The row index to iterate over.
         */
        RowIterator(const SparseMatrix &matrix, int row)
            : matrix(matrix), index(matrix.row_start[row]), end(matrix.row_start[row + 1]) {}

        /**
         * @brief Check if the iterator is valid (i.e., it has more elements to traverse).
         * @return True if there are more elements, false otherwise.
         */
        bool valid() const { return index < end; }

        /**
         * @brief Move to the next element in the row.
         */
        void next() { ++index; }

        /**
         * @brief Get the value of the current element.
         * @return The value of the current element.
         */
        double value() const { return matrix.elements[index].value; }

        /**
         * @brief Get the column index of the current element.
         * @return The column index of the current element.
         */
        int col() const { return matrix.elements[index].col; }
    };

    /**
     * @brief Get an iterator for a specific row.
     *
     * @param row The row index to iterate over.
     * @return A `RowIterator` for the specified row.
     */
    RowIterator rowIterator(int row) const {
        if (row_start.empty()) {
            const_cast<SparseMatrix *>(this)->buildRowStart(); // build it lazily if not done yet
        }
        return RowIterator(*this, row);
    }

    /**
     * @brief Perform a function for each non-zero element in every row.
     *
     * This function iterates over each row in the matrix and applies the given function
     * to each non-zero element in the row.
     *
     * @tparam Func A callable that accepts two arguments: column index and value of the element.
     * @param func The function to apply to each non-zero element.
     */
    template <typename Func>
    void forEachRow(Func func) const {
        if (row_start.empty()) {
            const_cast<SparseMatrix *>(this)->buildRowStart(); // ensure row_start is built
        }
        for (int row_idx = 0; row_idx < num_rows; ++row_idx) {
            RowIterator it = rowIterator(row_idx);
            while (it.valid()) {
                func(row_idx, it.col(), it.value());
                it.next();
            }
        }
    }

    /**
     * @brief Convert the sparse matrix to a dense matrix format.
     *
     * The dense matrix has the same dimensions as the sparse matrix, with zero values
     * where there are no non-zero elements.
     *
     * @return A 2D vector representing the dense matrix.
     */
    std::vector<std::vector<double>> toDense() const {
        std::vector<std::vector<double>> dense(num_rows, std::vector<double>(num_cols, 0.0));
        forEachRow([&dense](int row, int col, double value) { dense[row][col] = value; });
        return dense;
    }
};
