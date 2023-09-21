#include "internal.hpp"

namespace CaDiCaL {

using namespace std;

/*------------------------------------------------------------------------*/

// Enable proof logging and checking by allocating a 'Proof' object.

void Internal::new_proof_on_demand () {
  if (!proof) {
    proof = new Proof (this);
    LOG ("connecting proof to internal solver");
    setup_lrat_builder ();
  }
}

void Internal::setup_lrat_builder () {
  if (lratbuilder) return;
  if (opts.externallrat) {
    lratbuilder = new LratBuilder (this);
    LOG ("PROOF connecting lrat proof chain builder");
    proof->connect (lratbuilder);
  }
}

void Internal::force_lrat () {
  if (lrat || lratbuilder) return;
  lrat = true;
}

void Internal::connect_proof_tracer (Tracer *tracer, bool antecedents) {
  new_proof_on_demand ();
  if (antecedents) force_lrat ();
  proof->connect (tracer);
  tracers.push_back (tracer);
}

void Internal::connect_proof_tracer (InternalTracer *tracer, bool antecedents) {
  new_proof_on_demand ();
  if (antecedents) force_lrat ();
  tracer->connect_internal (this);
  proof->connect (tracer);
  tracers.push_back (tracer);
}

void Internal::connect_proof_tracer (StatTracer *tracer, bool antecedents) {
  new_proof_on_demand ();
  if (antecedents) force_lrat ();
  tracer->connect_internal (this);
  proof->connect (tracer);
  stat_tracers.push_back (tracer);
}

void Internal::connect_proof_tracer (FileTracer *tracer, bool antecedents) {
  new_proof_on_demand ();
  if (antecedents) force_lrat ();
  tracer->connect_internal (this);
  proof->connect (tracer);
  file_tracers.push_back (tracer);
}

bool Internal::disconnect_proof_tracer (Tracer *tracer) {
  auto t = tracers.begin ();
  while (t != tracers.end ()) {
    if (*t == tracer) {
      tracers.erase (t);
      proof->disconnect (tracer);
      return true;
    }
    t++;
  }
  return false;
}

bool Internal::disconnect_proof_tracer (StatTracer *tracer) {
  auto t = stat_tracers.begin ();
  while (t != stat_tracers.end ()) {
    if (*t == tracer) {
      stat_tracers.erase (t);
      proof->disconnect (tracer);
      return true;
    }
    t++;
  }
  return false;
}

bool Internal::disconnect_proof_tracer (FileTracer *tracer) {
  auto t = file_tracers.begin ();
  while (t != file_tracers.end ()) {
    if (*t == tracer) {
      file_tracers.erase (t);
      proof->disconnect (tracer);
      return true;
    }
    t++;
  }
  return false;
}

void Proof::disconnect (Tracer *t) {
  tracers.erase (std::remove (tracers.begin (), tracers.end (), t), tracers.end ());
}

// Enable proof tracing.

void Internal::trace (File *file) {
  new_proof_on_demand ();
  FileTracer * ft;
  if (opts.veripb) {
    LOG ("PROOF connecting veripb tracer");
    bool antecedents = opts.veripb == 1 || opts.veripb == 2;
    bool deletions = opts.veripb == 2 || opts.veripb == 4;
    ft = new VeripbTracer (this, file, opts.binary, antecedents, deletions);    
    if (antecedents) force_lrat ();
  } else if (opts.frat) {
    LOG ("PROOF connecting frat tracer");
    ft = new FratTracer (this, file, opts.binary, opts.frat == 1);
    if (opts.frat == 1) force_lrat ();
  } else if (opts.lrat) {
    LOG ("PROOF connecting lrat tracer");
    ft = new LratTracer (this, file, opts.binary);  
    force_lrat ();
  } else {
    LOG ("PROOF connecting drat tracer");
    ft = new DratTracer (this, file, opts.binary);    
  }
  assert (ft);
  proof->connect (ft);
  file_tracers.push_back (ft);
}

// Enable proof checking.

void Internal::check () {
  new_proof_on_demand ();
  if (opts.checkproof > 1) {
    StatTracer * lratchecker = new LratChecker (this);
    LOG ("PROOF connecting lrat proof checker");
    force_lrat ();
    proof->connect (lratchecker);
    stat_tracers.push_back (lratchecker);
  }
  if (opts.checkproof == 1 || opts.checkproof == 3) {
    StatTracer* checker = new Checker (this);
    LOG ("PROOF connecting proof checker");
    proof->connect (checker);
    stat_tracers.push_back (checker);
  }
}

// We want to close a proof trace and stop checking as soon we are done.

void Internal::close_trace () {
  for (auto & tracer : file_tracers)
    tracer->close ();
}

// We can flush a proof trace file before actually closing it.

void Internal::flush_trace () {
  for (auto & tracer : file_tracers)
    tracer->flush ();
}

/*------------------------------------------------------------------------*/

Proof::Proof (Internal *s)
    : internal (s), lratbuilder (0) {
  LOG ("PROOF new");
}

Proof::~Proof () { LOG ("PROOF delete"); }

/*------------------------------------------------------------------------*/

inline void Proof::add_literal (int internal_lit) {
  const int external_lit = internal->externalize (internal_lit);
  clause.push_back (external_lit);
}

inline void Proof::add_literals (Clause *c) {
  for (auto const &lit : *c)
    add_literal (lit);
}

inline void Proof::add_literals (const vector<int> &c) {
  for (auto const &lit : c)
    add_literal (lit);
}

/*------------------------------------------------------------------------*/

void Proof::add_original_clause (uint64_t id, bool r, const vector<int> &c) {
  LOG (c, "PROOF adding original internal clause");
  add_literals (c);
  clause_id = id;
  redundant = r;
  add_original_clause ();
}

void Proof::add_external_original_clause (uint64_t id, bool r,
                                          const vector<int> &c, bool restore) {
  // literals of c are already external
  assert (clause.empty ());
  for (auto const &lit : c)
    clause.push_back (lit);
  clause_id = id;
  redundant = r;
  add_original_clause (restore);
}

void Proof::delete_external_original_clause (uint64_t id, bool r,
                                             const vector<int> &c) {
  // literals of c are already external
  assert (clause.empty ());
  for (auto const &lit : c)
    clause.push_back (lit);
  clause_id = id;
  redundant = r;
  delete_clause ();
}

void Proof::add_derived_empty_clause (uint64_t id,
                                      const vector<uint64_t> &chain) {
  LOG ("PROOF adding empty clause");
  assert (clause.empty ());
  assert (proof_chain.empty ());
  for (const auto &cid : chain)
    proof_chain.push_back (cid);
  clause_id = id;
  redundant = false;
  add_derived_clause ();
}

void Proof::add_derived_unit_clause (uint64_t id, int internal_unit,
                                     const vector<uint64_t> &chain) {
  LOG ("PROOF adding unit clause %d", internal_unit);
  assert (proof_chain.empty ());
  assert (clause.empty ());
  add_literal (internal_unit);
  for (const auto &cid : chain)
    proof_chain.push_back (cid);
  clause_id = id;
  redundant = false;
  add_derived_clause ();
}

/*------------------------------------------------------------------------*/

void Proof::add_derived_clause (Clause *c, const vector<uint64_t> &chain) {
  LOG (c, "PROOF adding to proof derived");
  assert (clause.empty ());
  assert (proof_chain.empty ());
  add_literals (c);
  for (const auto &cid : chain)
    proof_chain.push_back (cid);
  clause_id = c->id;
  redundant = c->redundant;
  add_derived_clause ();
}

void Proof::add_derived_clause (uint64_t id, bool r, const vector<int> &c,
                                const vector<uint64_t> &chain) {
  LOG (internal->clause, "PROOF adding derived clause");
  assert (clause.empty ());
  assert (proof_chain.empty ());
  for (const auto &lit : c)
    add_literal (lit);
  for (const auto &cid : chain)
    proof_chain.push_back (cid);
  clause_id = id;
  redundant = r;
  add_derived_clause ();
}

void Proof::delete_clause (Clause *c) {
  LOG (c, "PROOF deleting from proof");
  assert (clause.empty ());
  add_literals (c);
  clause_id = c->id;
  redundant = c->redundant;
  delete_clause ();
}

void Proof::delete_clause (uint64_t id, bool r, const vector<int> &c) {
  LOG (c, "PROOF deleting from proof");
  assert (clause.empty ());
  add_literals (c);
  clause_id = id;
  redundant = r;
  delete_clause ();
}

void Proof::weaken_minus (Clause *c) {
  LOG (c, "PROOF weaken minus of ");
  assert (clause.empty ());
  add_literals (c);
  clause_id = c->id;
  weaken_minus ();
}

void Proof::weaken_minus (uint64_t id, const vector<int> &c) {
  LOG (c, "PROOF deleting from proof");
  assert (clause.empty ());
  add_literals (c);
  clause_id = id;
  weaken_minus ();
}

void Proof::weaken_plus (Clause *c) {
  weaken_minus (c);
  delete_clause(c);
}

void Proof::weaken_plus (uint64_t id, const vector<int> &c) {
  weaken_minus (id, c);
  delete_clause(id, false, c);
}

void Proof::delete_unit_clause (uint64_t id, const int lit) {
  LOG ("PROOF deleting unit from proof %d", lit);
  assert (clause.empty ());
  add_literal (lit);
  clause_id = id;
  redundant = false;
  delete_clause ();
}

void Proof::finalize_clause (Clause *c) {
  LOG (c, "PROOF finalizing clause");
  assert (clause.empty ());
  add_literals (c);
  clause_id = c->id;
  finalize_clause ();
}

void Proof::finalize_clause (uint64_t id, const vector<int> &c) {
  LOG (c, "PROOF finalizing clause");
  assert (clause.empty ());
  for (const auto &lit : c)
    add_literal (lit);
  clause_id = id;
  finalize_clause ();
}

void Proof::finalize_unit (uint64_t id, int lit) {
  LOG ("PROOF finalizing clause %d", lit);
  assert (clause.empty ());
  add_literal (lit);
  clause_id = id;
  finalize_clause ();
}

void Proof::finalize_external_unit (uint64_t id, int lit) {
  LOG ("PROOF finalizing clause %d", lit);
  assert (clause.empty ());
  clause.push_back (lit);
  clause_id = id;
  finalize_clause ();
}

/*------------------------------------------------------------------------*/

// During garbage collection clauses are shrunken by removing falsified
// literals. To avoid copying the clause, we provide a specialized tracing
// function here, which traces the required 'add' and 'remove' operations.

void Proof::flush_clause (Clause *c) {
  LOG (c, "PROOF flushing falsified literals in");
  assert (clause.empty ());
  for (int i = 0; i < c->size; i++) {
    int internal_lit = c->literals[i];
    if (internal->fixed (internal_lit) < 0) {
      const unsigned uidx = internal->vlit (-internal_lit);
      uint64_t id = internal->unit_clauses[uidx];
      assert (id);
      proof_chain.push_back (id);
      continue;
    }
    add_literal (internal_lit);
  }
  proof_chain.push_back (c->id);
  redundant = c->redundant;
  int64_t id = ++internal->clause_id;
  clause_id = id;
  add_derived_clause ();
  delete_clause (c);
  c->id = id;
}

// While strengthening clauses, e.g., through self-subsuming resolutions,
// during subsumption checking, we have a similar situation, except that we
// have to remove exactly one literal.  Again the following function allows
// to avoid copying the clause and instead provides tracing of the required
// 'add' and 'remove' operations.

void Proof::strengthen_clause (Clause *c, int remove,
                               const vector<uint64_t> &chain) {
  LOG (c, "PROOF strengthen by removing %d in", remove);
  assert (clause.empty ());
  for (int i = 0; i < c->size; i++) {
    int internal_lit = c->literals[i];
    if (internal_lit == remove)
      continue;
    add_literal (internal_lit);
  }
  int64_t id = ++internal->clause_id;
  clause_id = id;
  redundant = c->redundant;
  for (const auto &cid : chain)
    proof_chain.push_back (cid);
  add_derived_clause ();
  delete_clause (c);
  c->id = id;
}

void Proof::otfs_strengthen_clause (Clause *c, const std::vector<int> &old,
                                    const vector<uint64_t> &chain) {
  LOG (c, "PROOF otfs strengthen");
  assert (clause.empty ());
  for (int i = 0; i < c->size; i++) {
    int internal_lit = c->literals[i];
    add_literal (internal_lit);
  }
  int64_t id = ++internal->clause_id;
  clause_id = id;
  redundant = c->redundant;
  for (const auto &cid : chain)
    proof_chain.push_back (cid);
  add_derived_clause ();
  delete_clause (c->id, c->redundant, old);
  c->id = id;
}

void Proof::strengthen (uint64_t id) {
  clause_id = id;
  strengthen ();
}

/*------------------------------------------------------------------------*/

void Proof::add_original_clause (bool restore) {
  LOG (clause, "PROOF adding original external clause");
  assert (clause_id);

  if (lratbuilder)
    lratbuilder->add_original_clause (clause_id, clause);
  for (auto & tracer : tracers) {
    tracer->add_original_clause (clause_id, false, clause, restore);
  }
  clause.clear ();
  clause_id = 0;
}

void Proof::add_derived_clause () {
  LOG (clause, "PROOF adding derived external clause (redundant: %d)", redundant);
  assert (clause_id);
  if (lratbuilder) {
    proof_chain = lratbuilder->add_clause_get_proof (clause_id, clause);
  }
  for (auto & tracer : tracers) {
    tracer->add_derived_clause (clause_id, redundant, clause, proof_chain);
  }
  proof_chain.clear ();
  clause.clear ();
  clause_id = 0;
}

void Proof::delete_clause () {
  LOG (clause, "PROOF deleting external clause");
  if (lratbuilder)
    lratbuilder->delete_clause (clause_id, clause);
  for (auto & tracer : tracers) {
    tracer->delete_clause (clause_id, redundant, clause);
  }
  clause.clear ();
  clause_id = 0;
}

void Proof::weaken_minus () {
  LOG (clause, "PROOF marking as clause to restore");
  for (auto & tracer : tracers) {
    tracer->weaken_minus (clause_id, clause);
  }
  clause.clear ();
  clause_id = 0;
}

void Proof::strengthen () {
  LOG ("PROOF strengthen clause with id %" PRId64, clause_id);
  for (auto & tracer : tracers) {
    tracer->strengthen (clause_id);
  }
  clause_id = 0;
}

void Proof::finalize_clause () {
  for (auto & tracer : tracers) {
    tracer->finalize_clause (clause_id, clause);
  }
  clause.clear ();
  clause_id = 0;
}

void Proof::finalize_proof (uint64_t id) {
  for (auto & tracer : tracers) {
    tracer->finalize_proof (id);
  }
}

void Proof::begin_proof (uint64_t id) {
  for (auto & tracer : tracers) {
    tracer->begin_proof (id);
  }
}


} // namespace CaDiCaL
