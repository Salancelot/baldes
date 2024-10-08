/**
 * @file Definitions.h
 * @brief Header file containing definitions and structures for the solver.
 *
 * This file includes various definitions, enumerations, and structures used in the solver.
 * It also provides documentation for each structure and its members, along with methods
 * and operators associated with them.
 *
 */
#pragma once

#include "Common.h"
#include "SparseMatrix.h"

struct BucketOptions {
    int depot         = 0;
    int end_depot     = N_SIZE - 1;
    int max_path_size = N_SIZE / 2;
};

enum class Direction { Forward, Backward };
enum class Stage { One, Two, Three, Four, Enumerate, Fix };
enum class ArcType { Job, Bucket, Jump };
enum class Mutability { Const, Mut };
enum class Full { Full, Partial, Reverse };
enum class Status { Optimal, Separation, NotOptimal, Error, Rollback };
enum class CutType { ThreeRow, FourRow, FiveRow };

// Comparator function for Stage enum
constexpr bool operator<(Stage lhs, Stage rhs) { return static_cast<int>(lhs) < static_cast<int>(rhs); }
constexpr bool operator>(Stage lhs, Stage rhs) { return rhs < lhs; }
constexpr bool operator<=(Stage lhs, Stage rhs) { return !(lhs > rhs); }
constexpr bool operator>=(Stage lhs, Stage rhs) { return !(lhs < rhs); }

const size_t num_words = (N_SIZE + 63) / 64; // This will be 2 for 100 clients

/**
 * @struct Interval
 * @brief Represents an interval with a duration and a horizon.
 *
 * The Interval struct is used to store information about an interval, which consists of a duration and a
 * horizon. The duration is represented by a double value, while the horizon is represented by an integer value.
 */
struct Interval {
    int interval;
    int horizon;

    Interval(double interval, int horizon) : interval(interval), horizon(horizon) {}
};

// ANSI color code for yellow
constexpr const char *yellow       = "\033[93m";
constexpr const char *vivid_yellow = "\033[38;5;226m"; // Bright yellow
constexpr const char *vivid_red    = "\033[38;5;196m"; // Bright red
constexpr const char *vivid_green  = "\033[38;5;46m";  // Bright green
constexpr const char *vivid_blue   = "\033[38;5;27m";  // Bright blue
constexpr const char *reset_color  = "\033[0m";
constexpr const char *blue         = "\033[34m";
constexpr const char *dark_yellow  = "\033[93m";

/**
 * @brief Prints an informational message with a specific format.
 *
 * This function prints a message prefixed with "[info] " where "info" is colored yellow.
 * The message format and arguments are specified by the caller.
 *
 */
template <typename... Args>
inline void print_info(fmt::format_string<Args...> format, Args &&...args) {
    // Print "[", then yellow "info", then reset color and print "] "
    fmt::print(fg(fmt::color::yellow), "[info] ");
    fmt::print(format, std::forward<Args>(args)...);
}

/**
 * @brief Prints a formatted heuristic message with a specific color scheme.
 *
 * This function prints a message prefixed with "[heuristic] " where "heuristic"
 * is displayed in a vivid blue color. The rest of the message is formatted
 * according to the provided format string and arguments.
 *
 */
template <typename... Args>
inline void print_heur(fmt::format_string<Args...> format, Args &&...args) {
    // Print "[", then yellow "info", then reset color and print "] "
    fmt::print(fg(fmt::color::blue), "[heuristic] ");
    fmt::print(format, std::forward<Args>(args)...);
}

/**
 * @brief Prints a formatted message with a specific prefix.
 *
 * This function prints a message prefixed with "[cut]" in green color.
 * The message is formatted according to the provided format string and arguments.
 *
 */
template <typename... Args>
inline void print_cut(fmt::format_string<Args...> format, Args &&...args) {
    // Print "[", then yellow "info", then reset color and print "] "
    fmt::print(fg(fmt::color::green), "[cut] ");
    fmt::print(format, std::forward<Args>(args)...);
}

/**
 * @brief Prints a formatted message with a blue "info" tag.
 *
 * This function prints a message prefixed with a blue "info" tag enclosed in square brackets.
 * The message is formatted according to the provided format string and arguments.
 *
 */
template <typename... Args>
inline void print_blue(fmt::format_string<Args...> format, Args &&...args) {
    // Print "[", then blue "info", then reset color and print "] "
    fmt::print(fg(fmt::color::blue), "[debug] ");
    fmt::print(format, std::forward<Args>(args)...);
}

/**
 * @struct ModelData
 * @brief Represents the data structure for a mathematical model.
 *
 * This structure contains all the necessary components to define a mathematical
 * optimization model, including the coefficient matrix, constraints, objective
 * function coefficients, variable bounds, and types.
 *
 */
struct ModelData {
    SparseMatrix                     A_sparse;
    std::vector<std::vector<double>> A;     // Coefficient matrix for constraints
    std::vector<double>              b;     // Right-hand side coefficients for constraints
    std::vector<char>                sense; // Sense of each constraint ('<', '=', '>')
    std::vector<double>              c;     // Coefficients for the objective function
    std::vector<double>              lb;    // Lower bounds for variables
    std::vector<double>              ub;    // Upper bounds for variables
    std::vector<char>                vtype; // Variable types ('C', 'I', 'B')
    std::vector<std::string>         name;
    std::vector<std::string>         cname;
};

/**
 * @brief Prints the BALDES banner with formatted text.
 *
 * This function prints a banner for the BALDES algorithm, which is a Bucket Graph Labeling Algorithm
 * for Vehicle Routing. The banner includes bold and colored text to highlight the name and description
 * of the algorithm. The text is formatted to fit within a box of fixed width.
 *
 * The BALDES algorithm is a C++ implementation of a Bucket Graph-based labeling algorithm designed
 * to solve the Resource-Constrained Shortest Path Problem (RSCPP). This problem commonly arises as
 * a subproblem in state-of-the-art Branch-Cut-and-Price algorithms for various Vehicle Routing Problems (VRPs).
 */
inline void printBaldes() {
    constexpr auto bold  = "\033[1m";
    constexpr auto blue  = "\033[34m";
    constexpr auto reset = "\033[0m";

    fmt::print("\n");
    fmt::print("+------------------------------------------------------+\n");
    fmt::print("| {}{:<52}{} |\n", bold, "BALDES", reset); // Bold "BALDES"
    fmt::print("| {:<52} |\n", " ");
    fmt::print("| {}{:<52}{} |\n", blue, "BALDES, a Bucket Graph Labeling Algorithm", reset); // Blue text
    fmt::print("| {:<52} |\n", "for Vehicle Routing");
    fmt::print("| {:<52} |\n", " ");
    fmt::print("| {:<52} |\n", "a modern C++ implementation");
    fmt::print("| {:<52} |\n", "of the Bucket Graph-based labeling algorithm");
    fmt::print("| {:<52} |\n", "for the Resource-Constrained Shortest Path Problem");
    fmt::print("| {:<52} |\n", " ");
    fmt::print("| {:<52} |\n", "https://github.com/lseman/baldes");
    fmt::print("| {:<52} |\n", " ");

    fmt::print("+------------------------------------------------------+\n");
    fmt::print("\n");
}

/**
 * @brief Extracts model data from a given Gurobi model in a sparse format.
 *
 * This function retrieves the variables and constraints from the provided Gurobi model
 * and stores them in a ModelData structure. It handles variable bounds, objective coefficients,
 * variable types, and constraint information including the sparse representation of the constraint matrix.
 *
 */
inline ModelData extractModelDataSparse(GRBModel *model) {
    ModelData data;
    try {
        // Variables
        int     numVars = model->get(GRB_IntAttr_NumVars);
        GRBVar *vars    = model->getVars();

        // Reserve memory to avoid frequent reallocations
        data.ub.reserve(numVars);
        data.lb.reserve(numVars);
        data.c.reserve(numVars);
        data.vtype.reserve(numVars);
        data.name.reserve(numVars);

        for (int i = 0; i < numVars; ++i) {
            double ub = vars[i].get(GRB_DoubleAttr_UB);
            data.ub.push_back(ub > 1e10 ? std::numeric_limits<double>::infinity() : ub);

            double lb = vars[i].get(GRB_DoubleAttr_LB);
            data.lb.push_back(lb < -1e10 ? -std::numeric_limits<double>::infinity() : lb);

            data.c.push_back(vars[i].get(GRB_DoubleAttr_Obj));

            char type = vars[i].get(GRB_CharAttr_VType);
            data.vtype.push_back(type);

            data.name.push_back(vars[i].get(GRB_StringAttr_VarName));
        }

        // Constraints
        int          numConstrs = model->get(GRB_IntAttr_NumConstrs);
        SparseMatrix A_sparse;

        // Reserve memory for constraint matrices, estimate 10 non-zeros per row
        A_sparse.elements.reserve(numConstrs * 10);
        data.b.reserve(numConstrs);
        data.cname.reserve(numConstrs);
        data.sense.reserve(numConstrs);

        for (int i = 0; i < numConstrs; ++i) {
            GRBConstr  constr = model->getConstr(i);
            GRBLinExpr expr   = model->getRow(constr);

            int exprSize = expr.size();
            for (int j = 0; j < exprSize; ++j) {
                GRBVar var      = expr.getVar(j);
                double coeff    = expr.getCoeff(j);
                int    varIndex = var.index();

                // Populate SparseElement for A_sparse
                A_sparse.elements.push_back({i, varIndex, coeff});
            }

            data.cname.push_back(constr.get(GRB_StringAttr_ConstrName));
            data.b.push_back(constr.get(GRB_DoubleAttr_RHS));

            char sense = constr.get(GRB_CharAttr_Sense);
            data.sense.push_back(sense == GRB_LESS_EQUAL ? '<' : (sense == GRB_GREATER_EQUAL ? '>' : '='));
        }

        // Set matrix dimensions and build row_start
        A_sparse.num_cols = numVars;
        A_sparse.num_rows = numConstrs;
        A_sparse.buildRowStart();

        // Store the sparse matrix in data
        data.A_sparse = A_sparse;

    } catch (GRBException &e) {
        std::cerr << "Error code = " << e.getErrorCode() << std::endl;
        std::cerr << e.getMessage() << std::endl;
    }

    return data;
}

using DualSolution = std::vector<double>;

// Paralell sections

// Macro to define parallel sections
#define PARALLEL_SECTIONS(NAME, SCHEDULER, ...)            \
    auto NAME = parallel_sections(SCHEDULER, __VA_ARGS__); \
    stdexec::sync_wait(std::move(NAME));

// Macro to define individual sections (tasks)
#define SECTION [this]() -> void

// #define SECTION_CUSTOM(capture_list) [capture_list]() -> void
#define SECTION_CUSTOM(capture_list) [capture_list]() -> void

// Template for scheduling parallel sections
template <typename Scheduler, typename... Tasks>
auto parallel_sections(Scheduler &scheduler, Tasks &&...tasks) {
    // Schedule and combine all tasks using stdexec::when_all
    return stdexec::when_all((stdexec::schedule(scheduler) | stdexec::then(std::forward<Tasks>(tasks)))...);
}
