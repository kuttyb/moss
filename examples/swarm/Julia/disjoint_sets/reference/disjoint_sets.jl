# Frozen reference implementation for Julia Disjoint Sets (Union-Find)
# Derived from JuliaCollections/DataStructures.jl (IntDisjointSets)

"""
    IntDisjointSets(n::Integer)

A disjoint sets data structure for integers 1:n (or 0:n-1).
Each element is initially in its own single-element set.
"""
mutable struct IntDisjointSets
    parents::Vector{Int}
    ranks::Vector{Int}
    ngroups::Int

    function IntDisjointSets(n::Integer)
        parents = Vector{Int}(undef, n)
        for i in 1:n
            parents[i] = i
        end
        ranks = zeros(Int, n)
        new(parents, ranks, n)
    end
end

"""
    num_groups(s::IntDisjointSets)

Return the number of disjoint sets currently in the collection.
"""
num_groups(s::IntDisjointSets) = s.ngroups

"""
    num_elements(s::IntDisjointSets)

Return the total number of elements.
"""
num_elements(s::IntDisjointSets) = length(s.parents)

"""
    find_root!(s::IntDisjointSets, x::Integer)

Find the root of the set containing element `x`.
Applies path compression iteratively so subsequent finds are O(1) amortized.
"""
function find_root!(s::IntDisjointSets, x::Integer)
    # Find root iteratively
    root = x
    while root != s.parents[root]
        root = s.parents[root]
    end

    # Path compression: update parents along the path to point directly to root
    curr = x
    while curr != root
        nxt = s.parents[curr]
        s.parents[curr] = root
        curr = nxt
    end

    return root
end

"""
    in_same_set(s::IntDisjointSets, x::Integer, y::Integer)

Check whether elements `x` and `y` belong to the same set.
"""
function in_same_set(s::IntDisjointSets, x::Integer, y::Integer)
    return find_root!(s, x) == find_root!(s, y)
end

"""
    union!(s::IntDisjointSets, x::Integer, y::Integer)

Merge the set containing `x` with the set containing `y`.
Uses union-by-rank to maintain balanced trees.
Returns the root of the merged set.
"""
function union!(s::IntDisjointSets, x::Integer, y::Integer)
    rx = find_root!(s, x)
    ry = find_root!(s, y)

    if rx != ry
        # Union by rank
        if s.ranks[rx] < s.ranks[ry]
            s.parents[rx] = ry
            s.ngroups -= 1
            return ry
        elseif s.ranks[rx] > s.ranks[ry]
            s.parents[ry] = rx
            s.ngroups -= 1
            return rx
        else
            s.parents[ry] = rx
            s.ranks[rx] += 1
            s.ngroups -= 1
            return rx
        end
    end
    return rx
end
