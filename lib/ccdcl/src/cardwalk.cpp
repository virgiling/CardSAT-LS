#include "internal.hpp"

namespace CaDiCaL {

inline void Internal::card_walk_save_minimum() {
  cardsat_ls->get_phase(phases.ls);
  for (auto i : vars) {
    if (!active(i))
      continue;
    if (vals[i])
      continue;
    phases.min[i] = phases.saved[i] = phases.ls[i];
  }
}

/*------------------------------------------------------------------------*/

int Internal::card_walk_round(int64_t limit, bool prev) {

  backtrack();
  if (propagated < trail.size() && !propagate()) {
    LOG("empty clause after root level propagation");
    learn_empty_clause();
    return 20;
  }

  stats.walk.count++;

  clear_watches();

  CARwatch_in_garbage = 0;
  if (last.collect.fixed < stats.all.fixed)
    garbage_collection();

  CARwatch_in_garbage = 1;

#ifndef QUIET
  if (localsearching) {
    assert(!force_phase_messages);
    force_phase_messages = true;
  }
#endif

  PHASE("walk", stats.walk.count,
        "random walk limit of %" PRId64 " propagations", limit);

  bool failed = false;
  level = 1;

  if (assumptions.empty()) {
    LOG("no assumptions so assigning all variables to decision phase");
  } else {
    LOG("assigning assumptions to their forced phase first");
    for (const auto lit : assumptions) {
      signed char tmp = val(lit);
      if (tmp > 0)
        continue;
      if (tmp < 0) {
        LOG("inconsistent assumption %d", lit);
        failed = true;
        break;
      }
      if (!active(lit))
        continue;
      tmp = sign(lit);
      const int idx = abs(lit);
      LOG("initial assign %d to assumption phase", tmp < 0 ? -idx : idx);
      vals[idx] = tmp;
      vals[-idx] = -tmp;
      assert(level == 1);
      var(idx).level = 1;
    }
    if (!failed)
      LOG("now assigning remaining variables to their decision phase");
  }

  level = 2;
  int res = 0;

  if (!failed) {
    int num_var = 0, clause_num = 0;
    for (const auto clause : clauses) {
      if (clause->garbage)
        continue;
      if (clause->redundant) {
        if (!opts.walkredundant)
          continue;
        if (!likely_to_be_kept_clause(clause))
          continue;
      }
      clause_num++;
    }
    for (const auto clause : CARclauses) {
      if (clause->garbage)
        continue;
      if (clause->redundant) {
        if (!opts.walkredundant)
          continue;
        if (!likely_to_be_kept_clause(clause))
          continue;
      }
      clause_num++;
    }
    cardsat_ls->allocate_memory(max_var, clause_num);
    for (const auto clause : clauses) {
      if (clause->garbage)
        continue;
      if (clause->redundant) {
        if (!opts.walkredundant)
          continue;
        if (!likely_to_be_kept_clause(clause))
          continue;
      }
      cardsat_ls->add_clause(clause->literals, clause->size, 1);
    }
    for (const auto clause : CARclauses) {
      if (clause->garbage)
        continue;
      if (clause->redundant) {
        if (!opts.walkredundant)
          continue;
        if (!likely_to_be_kept_clause(clause))
          continue;
      }
      cardsat_ls->add_clause(clause->literals, clause->size,
                             clause->CARbound());
    }

    // // TODO use cardsat-ls
    // std::vector<signed char> cardsat_assignment;
    // for (auto idx : vars) {
    //   if (!active(idx)) {
    //     cardsat_assignment.push_back(1);
    //   } else if (vals[idx]) {
    //     cardsat_assignment.push_back(vals[idx]);
    //   } else {
    //     cardsat_assignment.push_back(sign(decide_phase(idx, true)));
    //   }
    // }

    // cardsat_ls->init_assignment(cardsat_assignment);
    res = cardsat_ls->start_cardsat_ls(limit);
  } else {
    res = 20;
    PHASE("walk", stats.walk.count, "aborted due to inconsistent assumptions");
  }

  card_walk_save_minimum();

  copy_phases(phases.prev);

  for (auto idx : vars)
    if (active(idx))
      vals[idx] = vals[-idx] = 0;

  assert(level == 2);
  level = 0;

  clear_watches();
  connect_watches();

  cardsat_ls->free_memory();

#ifndef QUIET
  if (localsearching) {
    assert(force_phase_messages);
    force_phase_messages = false;
  }
#endif

  printf("c walk_round finished\n");

  return res;
}
} // namespace CaDiCaL
