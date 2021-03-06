#ifndef DCGP_EXPRESSION_H
#define DCGP_EXPRESSION_H

#include <audi/audi.hpp>
#include <initializer_list>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <dcgp/kernel.hpp>
#include <dcgp/type_traits.hpp>

namespace dcgp
{

/// A dCGP expression
/**
 * This class represents a mathematical expression as encoded using CGP and
 * contains algorithms to compute its value (numerical and symbolical) and its
 * derivatives as well as to mutate the expression.
 *
 * @tparam T expression type. Can be double, or a gdual type.
 *
 * @author Dario Izzo (dario.izzo@gmail.com)
 */
template <typename T>
class expression
{

private:
    // Static checks.
    static_assert(std::is_same<T, double>::value || is_gdual<T>::value,
                  "A d-CGP expression can only be operating on doubles or gduals");
    // SFINAE dust
    template <typename U>
    using functor_enabler = typename std::enable_if<
        std::is_same<U, double>::value || is_gdual<T>::value || std::is_same<U, std::string>::value, int>::type;

public:
    /// Constructor
    /** Constructs a dCGP expression
     *
     * @param[in] n number of inputs (independent variables)
     * @param[in] m number of outputs (dependent variables)
     * @param[in] r number of rows of the dCGP
     * @param[in] c number of columns of the dCGP
     * @param[in] l number of levels-back allowed in the dCGP
     * @param[in] arity arity of the basis functions
     * @param[in] f function set. An std::vector of dcgp::kernel<expression::type>
     * @param[in] seed seed for the random number generator (initial expression
     * and mutations depend on this)
     */
    expression(unsigned n,               // n. inputs
               unsigned m,               // n. outputs
               unsigned r,               // n. rows
               unsigned c,               // n. columns
               unsigned l,               // n. levels-back
               unsigned arity,           // basis functions' arity
               std::vector<kernel<T>> f, // functions
               unsigned seed             // seed for the pseudo-random numbers
               )
        : m_n(n), m_m(m), m_r(r), m_c(c), m_l(l), m_arity(arity), m_f(f), m_lb((arity + 1) * m_r * m_c + m_m, 0),
          m_ub((arity + 1) * m_r * m_c + m_m, 0), m_x((arity + 1) * m_r * m_c + m_m, 0), m_e(seed)
    {
        // Sanity checks
        if (n == 0) throw std::invalid_argument("Number of inputs is 0");
        if (m == 0) throw std::invalid_argument("Number of outputs is 0");
        if (c == 0) throw std::invalid_argument("Number of columns is 0");
        if (r == 0) throw std::invalid_argument("Number of rows is 0");
        if (l == 0) throw std::invalid_argument("Number of level-backs is 0");
        if (arity < 2) throw std::invalid_argument("Basis functions arity must be at least 2");
        if (f.size() == 0) throw std::invalid_argument("Number of basis functions is 0");

        // Bounds for the function genes
        for (auto i = 0u; i < ((arity + 1u) * m_r * m_c); i += (arity + 1u)) {
            m_ub[i] = static_cast<unsigned>(f.size() - 1u);
        }

        // Bounds for the output genes
        for (auto i = (arity + 1) * m_r * m_c; i < m_ub.size(); ++i) {
            m_ub[i] = m_n + m_r * m_c - 1u;
            if (m_l <= m_c) {
                m_lb[i] = m_n + m_r * (m_c - m_l);
            }
        }

        // Bounds for the node connection genes
        for (auto i = 0u; i < m_c; ++i) {
            for (auto j = 0u; j < m_r; ++j) {
                for (auto k = 0u; k < arity; ++k) {
                    m_ub[((i * m_r) + j) * (arity + 1u) + k + 1u] = m_n + i * m_r - 1u;
                    if (i >= m_l) {
                        m_lb[((i * m_r) + j) * (arity + 1u) + k + 1u] = m_n + m_r * (i - m_l);
                    }
                }
            }
        }

        // We generate a random expression
        for (auto i = 0u; i < m_x.size(); ++i) {
            m_x[i] = std::uniform_int_distribution<unsigned>(m_lb[i], m_ub[i])(m_e);
        }
        update_active();
    }

    /// Sets the chromosome
    /** Sets a given chromosome as genotype for the expression and updates
     * the active nodes and active genes information accordingly
     *
     * @param[in] x the new cromosome
     *
     * @throw std::invalid_argument if the chromosome is incompatible with
     * the expression (n.inputs, n.outputs, levels-back, etc.)
     */
    void set(const std::vector<unsigned> &x)
    {
        if (!is_valid(x)) {
            throw std::invalid_argument("Chromosome is incompatible");
        }
        m_x = x;
        update_active();
    }

    /// Gets the chromosome
    /**
     * Gets the chromosome encoding the current expression
     *
     * @return The chromosome
     */
    const std::vector<unsigned> &get() const
    {
        return m_x;
    }

    /// Gets the lower bounds
    /**
     * Gets the lower bounds for the genes
     *
     * @return An std::vector containing the lower bound for each gene
     */
    const std::vector<unsigned> &get_lb() const
    {
        return m_lb;
    }

    /// Gets the upper bounds
    /**
     * Gets the upper bounds for the genes
     *
     * @return An std::vector containing the upper bound for each gene
     */
    const std::vector<unsigned> &get_ub() const
    {
        return m_ub;
    }

    /// Gets the active genes
    /**
     * Gets the idx of the active genes in the current chromosome (numbering is
     * from 0)
     *
     * @return An std::vector containing the idx of the active genes in the
     * current chromosome
     */
    const std::vector<unsigned> &get_active_genes() const
    {
        return m_active_genes;
    }

    /// Gets the active nodes
    /**
     * Gets the idx of the active nodes in the current chromosome.
     * The numbering starts from 0 at the first input node to then follow PPSN
     * tutorial from Miller
     *
     * @return An std::vector containing the idx of the active nodes
     */
    const std::vector<unsigned> &get_active_nodes() const
    {
        return m_active_nodes;
    }

    /// Gets the number of inputs
    /**
     * Gets the number of inputs of the dCGP expression
     *
     * @return the number of inputs
     */
    unsigned get_n() const
    {
        return m_n;
    }

    /// Gets the number of outputs
    /**
     * Gets the number of outputs of the dCGP expression
     *
     * @return the number of outputs
     */
    unsigned get_m() const
    {
        return m_m;
    }

    /// Gets the number of rows
    /**
     * Gets the number of rows of the dCGP expression
     *
     * @return the number of rows
     */
    unsigned get_rows() const
    {
        return m_r;
    }

    /// Gets the number of columns
    /**
     * Gets the number of columns of the dCGP expression
     *
     * @return the number of columns
     */
    unsigned get_cols() const
    {
        return m_c;
    }

    /// Gets the number of levels-back
    /**
     * Gets the number of levels-back allowed for the dCGP expression
     *
     * @return the number of levels-back
     */
    unsigned get_levels_back() const
    {
        return m_l;
    }

    /// Gets the arity
    /**
     * Gets the arity of the basis functions of the dCGP expression
     *
     * @return the arity
     */
    unsigned get_arity() const
    {
        return m_arity;
    }

    /// Gets the function set
    /**
     * Gets the set of functions used in the dCGP expression
     *
     * @return an std::vector of kernels
     */
    const std::vector<kernel<T>> &get_f() const
    {
        return m_f;
    }

    /// Mutates one gene
    /**
     * Mutates exactly one gene within its allowed bounds.
     *
     * @param[in] idx index of the gene to me mutated
     *
     * @throw std::invalid_argument if \p idx is too large
     */
    void mutate(unsigned idx)
    {
        if (idx >= m_x.size()) {
            throw std::invalid_argument("idx of gene to be mutated is out of bounds");
        }
        // If only one value is allowed for the gene, (lb==ub),
        // then we will not do anything as mutation does not apply
        if (m_lb[idx] < m_ub[idx]) {
            unsigned new_value;
            do {
                new_value = std::uniform_int_distribution<unsigned>(m_lb[idx], m_ub[idx])(m_e);
            } while (new_value == m_x[idx]);
            m_x[idx] = new_value;
            update_active();
        }
    }

    /// Mutates multiple genes at once
    /**
     * Mutates multiple genes within their allowed bounds.
     *
     * @param[in] idxs vector of indexes of the genes to me mutated
     *
     * @throw std::invalid_argument if \p idx is too large
     */
    void mutate(std::vector<unsigned> idxs)
    {
        bool flag = false;
        for (auto i = 0u; i < idxs.size(); ++i) {
            if (idxs[i] >= m_x.size()) {
                throw std::invalid_argument("idx of gene to be mutated is out of bounds");
            }
            // If only one value is allowed for the gene, (lb==ub),
            // then we will not do anything as mutation does not apply
            if (m_lb[idxs[i]] < m_ub[idxs[i]]) {
                unsigned new_value;
                do {
                    new_value = std::uniform_int_distribution<unsigned>(m_lb[idxs[i]], m_ub[idxs[i]])(m_e);
                } while (new_value == m_x[idxs[i]]);
                m_x[idxs[i]] = new_value;
                flag = true;
            }
        }
        if (flag) update_active();
    }

    /// Mutates N random genes
    /**
     * Mutates a specified number of random genes within their bounds
     *
     * @param[in] N number of genes to be mutated
     *
     */
    void mutate_random(unsigned N)
    {
        bool flag = false;
        for (auto i = 0u; i < N; ++i) {
            // If only one value is allowed for the gene, (lb==ub),
            // then we will not do anything as mutation does not apply
            auto idx = std::uniform_int_distribution<unsigned>(0, m_lb.size() - 1)(m_e);
            if (m_lb[idx] < m_ub[idx]) {
                unsigned new_value;
                do {
                    new_value = std::uniform_int_distribution<unsigned>(m_lb[idx], m_ub[idx])(m_e);
                } while (new_value == m_x[idx]);
                m_x[idx] = new_value;
                flag = true;
            }
        }
        if (flag) update_active();
    }

    /// Mutates one of the active genes
    /**
     * Mutates \p N active genes within their allowed bounds.
     * The mutation can affect function genes, input genes and output genes.
     *
     * @param[in] N Number of active genes to be mutated
     *
     */
    void mutate_active(unsigned N = 1)
    {
        for (auto i = 0u; i < N; ++i) {
            unsigned idx
                = std::uniform_int_distribution<unsigned>(0, static_cast<unsigned>(m_active_genes.size() - 1u))(m_e);
            idx = m_active_genes[idx];
            mutate(idx);
        }
    }

    /// Mutates one of the active function genes
    /**
     * Mutates exactly one of the active function genes within its allowed bounds.
     */
    void mutate_active_fgene()
    {
        // If no active function gene exists, do nothing
        if (m_active_genes.size() > m_m) {
            unsigned idx = std::uniform_int_distribution<unsigned>(
                0, static_cast<unsigned>(m_active_genes.size() - 1u - m_m))(m_e);
            idx = m_active_genes[idx] - (m_active_genes[idx] % (m_arity + 1));
            mutate(idx);
        }
    }

    /// Mutates one of the active connection genes
    /**
     * Mutates exactly one of the active connection genes within its allowed
     * bounds.
     */
    void mutate_active_cgene()
    {
        // If no active function gene exists, do nothing
        if (m_active_genes.size() > m_m) {
            unsigned idx = std::uniform_int_distribution<unsigned>(
                0u, static_cast<unsigned>(m_active_genes.size() - 1u - m_m))(m_e);
            idx = m_active_genes[idx] - (m_active_genes[idx] % (m_arity + 1))
                  + std::uniform_int_distribution<unsigned>(1, m_arity)(m_e);
            mutate(idx);
        }
    }

    /// Mutates one of the active output genes
    /**
     * Mutates exactly one of the output genes within its allowed bounds.
     */
    void mutate_ogene()
    {
        unsigned idx;
        if (m_m > 1) {
            idx = std::uniform_int_distribution<unsigned>(static_cast<unsigned>(m_active_genes.size() - m_m),
                                                          static_cast<unsigned>(m_active_genes.size() - 1u))(m_e);

        } else {
            idx = static_cast<unsigned>(m_active_genes.size() - 1u);
        }
        idx = m_active_genes[idx];
        mutate(idx);
    }

    /// Evaluates the dCGP expression
    /**
     * This evaluates the dCGP expression. According to the template parameter
     * it will compute the value (double) the Taylor expansion (gdual) or a
     * symbolic representation (std::string). Any other type will result in a
     * compilation-time error (SFINAE).
     *
     * @param[in] in an std::vector containing the values where the dCGP
     * expression has to be computed (doubles, gduals or strings)
     *
     * @return The value of the function (an std::vector)
     */
    template <typename U, functor_enabler<U> = 0>
    std::vector<U> operator()(const std::vector<U> &in) const
    {
        if (in.size() != m_n) {
            throw std::invalid_argument("Input size is incompatible");
        }
        std::vector<U> retval(m_m);
        std::map<unsigned, U> node;
        std::vector<U> function_in(m_arity);
        for (auto i : m_active_nodes) {
            if (i < m_n) {
                node[i] = in[i];
            } else {
                unsigned idx = (i - m_n) * (m_arity + 1); // position in the chromosome of the current node
                for (auto j = 0u; j < m_arity; ++j) {
                    function_in[j] = node[m_x[idx + j + 1]];
                }
                node[i] = m_f[m_x[idx]](function_in);
            }
        }
        for (auto i = 0u; i < m_m; ++i) {
            retval[i] = node[m_x[(m_r * m_c) * (m_arity + 1) + i]];
        }
        return retval;
    }

    /// Evaluates the dCGP expression
    /**
     * This evaluates the dCGP expression. According to the template parameter
     * it will compute the value (double) the Taylor expansion (gdual) or a
     * symbolic representation (std::string). Any other type will result in a
     * compilation-time error (SFINAE). This is identical to the other overload
     * and is provided only for convenience
     *
     * @param[in] in an initializer list containing the values where the dCGP
     * expression has to be computed (doubles, gduals or strings)
     *
     * @return The value of the function (an std::vector)
     */
    template <typename U, functor_enabler<U> = 0>
    std::vector<U> operator()(const std::initializer_list<U> &in) const
    {
        std::vector<U> dummy(in);
        return (*this)(dummy);
    }

    /// Overloaded stream operator
    /**
     * Will return a formatted string containing a human readable representation
     * of the class
     *
     * @return std::string containing a human-readable representation of the
     * problem.
     */
    friend std::ostream &operator<<(std::ostream &os, const expression &d)
    {
        audi::stream(os, "d-CGP Expression:\n");
        audi::stream(os, "\tNumber of inputs:\t\t", d.m_n, '\n');
        audi::stream(os, "\tNumber of outputs:\t\t", d.m_m, '\n');
        audi::stream(os, "\tNumber of rows:\t\t\t", d.m_r, '\n');
        audi::stream(os, "\tNumber of columns:\t\t", d.m_c, '\n');
        audi::stream(os, "\tNumber of levels-back allowed:\t", d.m_l, '\n');
        audi::stream(os, "\tBasis function arity:\t\t", d.m_arity, '\n');
        audi::stream(os, "\n\tResulting lower bounds:\t", d.m_lb);
        audi::stream(os, "\n\tResulting upper bounds:\t", d.m_ub, '\n');
        audi::stream(os, "\n\tCurrent expression (encoded):\t", d.m_x, '\n');
        audi::stream(os, "\tActive nodes:\t\t\t", d.m_active_nodes, '\n');
        audi::stream(os, "\tActive genes:\t\t\t", d.m_active_genes, '\n');
        audi::stream(os, "\n\tFunction set:\t\t\t", d.m_f, '\n');
        return os;
    }

protected:
    /// Validity of a chromosome
    /**
     * Checks if a chromosome (i.e. a sequence of integers) is a valid expression
     * by verifying its length and the bounds
     *
     * @param[in] x chromosome
     */
    bool is_valid(const std::vector<unsigned> &x) const
    {
        // Checking for length
        if (x.size() != m_lb.size()) {
            return false;
        }

        // Checking for bounds on all genes
        for (auto i = 0u; i < x.size(); ++i) {
            if ((x[i] > m_ub[i]) || (x[i] < m_lb[i])) {
                return false;
            }
        }
        return true;
    }

    // Updates the list of active nodes
    void update_active()
    {
        assert(m_x.size() == m_lb.size());

        // First we update the active nodes
        std::vector<unsigned> current(m_m), next;
        m_active_nodes.clear();
        // At the beginning, current contains only the output nodes connections
        for (auto i = 0u; i < m_m; ++i) {
            current[i] = m_x[(m_arity + 1) * m_r * m_c + i];
        }
        do {
            m_active_nodes.insert(m_active_nodes.end(), current.begin(), current.end());

            for (auto node_id : current) {
                if (node_id >= m_n) // we insert the input nodes connections as they do
                                    // not have any
                {
                    for (auto i = 1u; i <= m_arity; ++i) {
                        next.push_back(m_x[(node_id - m_n) * (m_arity + 1) + i]);
                    }
                } else {
                    m_active_nodes.push_back(node_id);
                }
            }
            // We remove duplicates to avoid processing them and thus having a 2^N
            // complexity
            std::sort(next.begin(), next.end());
            next.erase(std::unique(next.begin(), next.end()), next.end());
            current = next;
            next.clear();
        } while (current.size() > 0);

        // We remove duplicates and keep m_active_nodes sorted
        std::sort(m_active_nodes.begin(), m_active_nodes.end());
        m_active_nodes.erase(std::unique(m_active_nodes.begin(), m_active_nodes.end()), m_active_nodes.end());

        // Then the active genes
        m_active_genes.clear();
        for (auto i = 0u; i < m_active_nodes.size(); ++i) {
            if (m_active_nodes[i] >= m_n) {
                unsigned idx = (m_active_nodes[i] - m_n) * (m_arity + 1);
                for (auto j = 0u; j <= m_arity; ++j) {
                    m_active_genes.push_back(idx + j);
                }
            }
        }
        for (auto i = 0u; i < m_m; ++i) {
            m_active_genes.push_back(m_r * m_c * (m_arity + 1) + i);
        }
    }

private:
    // number of inputs
    unsigned m_n;
    // number of outputs
    unsigned m_m;
    // number of rows
    unsigned m_r;
    // number of columns
    unsigned m_c;
    // number of levels_back allowed
    unsigned m_l;
    // function arity
    unsigned m_arity;

    // the functions allowed
    std::vector<kernel<T>> m_f;
    // lower and upper bounds on all genes
    std::vector<unsigned> m_lb;
    std::vector<unsigned> m_ub;
    // active nodes idx (guaranteed to be always sorted)
    std::vector<unsigned> m_active_nodes;
    // active genes idx
    std::vector<unsigned> m_active_genes;
    // the encoded chromosome
    std::vector<unsigned> m_x;
    // the random engine for the class
    std::default_random_engine m_e;
    // The expression type
    using type = T;
};

} // end of namespace dcgp

#endif // DCGP_EXPRESSION_H
