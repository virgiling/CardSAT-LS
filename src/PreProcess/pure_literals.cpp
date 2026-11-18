#include "preprocess.hpp"

#include <cassert>
#include <cstdlib>
#include <vector>

bool Preprocessor::simplify_pure_literals(PureLiteralQueue &candidates,
                                          bool &changed) {
  for (int v = 1; v <= vars; ++v)
    if (!candidates.eliminated[v] &&
        (!candidates.positive[v] || !candidates.negative[v]))
      candidates.enqueue(v);
  if (candidates.pending.empty())
    return true;

  std::vector<int> removed(clauses + 1, 0);
  std::vector<unsigned char> processed(vars + 1, 0);
  std::size_t head = 0;
  while (head < candidates.pending.size()) {
    const int v = candidates.pending[head++];
    candidates.queued[v] = 0;
    if (candidates.eliminated[v] ||
        (candidates.positive[v] && candidates.negative[v]))
      continue;
    if (!candidates.positive[v] && !candidates.negative[v]) {
      candidates.eliminated[v] = 1;
      continue;
    }

    const int original = mapfrom[v];
    const int value = candidates.positive[v] ? 1 : -1;
    mapval[original] = value;
    mapto[original] = 0;
    candidates.eliminated[v] = 1;
    processed[v] = 1;
    changed = true;

    vec<int> &occurrences = value > 0 ? occurp[v] : occurn[v];
    for (int j = 0; j < occurrences.size();) {
      const int id = occurrences[j++];
      int count = 1;
      // Occurrences of the same row are consecutive.
      while (j < occurrences.size() && occurrences[j] == id) {
        ++count;
        ++j;
      }
      if (clause_delete[id])
        continue;

      candidates.count(value * v) -= count;
      removed[id] += count;
      degree[id] -= count;
      if (degree[id] == 0) {
        // Fixed literals remain in the row until compaction.
        for (int k = 0; k < clause[id].size(); ++k) {
          const int lit = clause[id][k];
          if (!processed[std::abs(lit)])
            candidates.remove_literal(lit);
        }
        clause_delete[id] = 1;
        clause[id].setsize(0);
      } else if (degree[id] > clause[id].size() - removed[id]) {
        return false;
      }
    }
  }

  for (int id = 1; id <= clauses; ++id) {
    if (clause_delete[id] || !removed[id])
      continue;
    int kept = 0;
    for (int j = 0; j < clause[id].size(); ++j) {
      const int lit = clause[id][j];
      if (!processed[std::abs(lit)])
        clause[id][kept++] = lit;
    }
    assert(kept == clause[id].size() - removed[id]);
    clause[id].setsize(kept);
  }
  return true;
}
