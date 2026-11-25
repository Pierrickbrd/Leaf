//! Reading orders that include one another, resolved before anything reaches the database.
//!
//! Pure on purpose: the scan flattens with it and the announcement of an import refuses with
//! it, and the two have to give the same answer about the same file.

use std::collections::{BTreeMap, HashMap};

use crate::metadata::sidecars::{OrderJson, StepJson};
use crate::store::text::search_key;

/// The most steps one order may hold once its inclusions are read in place. An order that
/// includes the next one twice, fourteen times over, is 8 192 steps from a file of a few
/// hundred bytes; without a ceiling a small `universe.json` fills the database.
pub const MOST_FLAT_STEPS: usize = 4096;

/// The most orders of one universe that get steps. A file of forty thousand one-line orders
/// was two seconds of comparing each with the ones before it, and forty thousand rows each
/// time anything was scanned; the ones past this are recorded without steps.
pub const MOST_ORDERS: usize = 1024;

/// How many steps may be looked at, kept or not, while flattening one order. Steps that
/// produce nothing (refused, or included orders that are empty) cost time without counting
/// toward `MOST_FLAT_STEPS`, and a chain of those doubles just the same.
pub const MOST_STEPS_LOOKED_AT: usize = MOST_FLAT_STEPS * 16;

/// What all the orders of one universe may add up to once flattened. A ceiling per order
/// alone lets two hundred orders, each including the one before and each just under it,
/// write tens of thousands of rows from a file of a few kilobytes.
pub const MOST_UNIVERSE_STEPS: usize = MOST_FLAT_STEPS * 4;

/// How many steps may be looked at across a whole universe, for the same reason as the
/// ceiling above: a chain of ten thousand orders, each reading the one after it, is fifty
/// million visits if every order walks its own way down.
pub const MOST_UNIVERSE_LOOKS: usize = MOST_STEPS_LOOKED_AT * 4;

/// What an order is called when something refers to it: its `id` if it wrote one, else the
/// search key of its name, else its rank. The scan and the announcement both go through
/// here, so an `id` means the same to both.
pub fn declared_id(order: &OrderJson, at: usize) -> String {
    order
        .id
        .as_deref()
        .map(str::trim)
        .filter(|one| !one.is_empty())
        .map(str::to_string)
        .or_else(|| {
            order
                .name
                .as_deref()
                .map(search_key)
                .filter(|one| !one.is_empty())
        })
        .unwrap_or_else(|| (at + 1).to_string())
}

fn carries_more_than_include(step: &StepJson) -> bool {
    step.work.is_some()
        || step.unit.is_some()
        || step.edition.is_some()
        || step.from.is_some()
        || step.to.is_some()
}

/// Every inclusion that cannot be honoured, by (order, step), with why: it designates no
/// order, it carries other keys, or it closes a loop. See `Plan::refused`.
pub fn refused(orders: &[OrderJson]) -> BTreeMap<(usize, usize), String> {
    Plan::new(orders).refused
}

/// What a universe's orders say about one another, worked out once: who designates whom,
/// which inclusions are refused. Resolving an include by scanning every order for every
/// step made a file of ten thousand orders a hundred million comparisons.
pub struct Plan<'a> {
    orders: &'a [OrderJson],
    by_id: HashMap<String, usize>,
    by_name: HashMap<String, usize>,
    /// Per order, per step: the order an include designates, whether or not it is refused.
    targets: Vec<Vec<Option<usize>>>,
    /// Every inclusion that cannot be honoured, with why.
    ///
    /// A loop refuses *each* of its inclusions and not just the last one met. Which one
    /// closes the loop depends on the order it is entered from, so refusing only one would
    /// give `a` half of `b` and `b` half of `a`: two answers to the same question, from the
    /// same file.
    pub refused: BTreeMap<(usize, usize), String>,
}

impl<'a> Plan<'a> {
    pub fn new(orders: &'a [OrderJson]) -> Self {
        let mut by_id = HashMap::new();
        let mut by_name = HashMap::new();
        for (at, order) in orders.iter().enumerate() {
            // An order whose identifier is already taken is dropped by the scan, and an
            // include must not reach what the scan drops by going round by its name.
            if by_id.contains_key(&declared_id(order, at)) {
                continue;
            }
            by_id.insert(declared_id(order, at), at);
            if let Some(key) = order
                .name
                .as_deref()
                .map(search_key)
                .filter(|key| !key.is_empty())
            {
                by_name.entry(key).or_insert(at);
            }
        }
        let mut plan = Plan {
            orders,
            by_id,
            by_name,
            targets: Vec::new(),
            refused: BTreeMap::new(),
        };
        plan.targets = orders
            .iter()
            .map(|order| {
                order
                    .steps
                    .iter()
                    .map(|step| step.include.as_deref().and_then(|one| plan.designated(one)))
                    .collect()
            })
            .collect();
        plan.refuse();
        plan
    }

    fn designated(&self, include: &str) -> Option<usize> {
        let wanted = include.trim();
        if wanted.is_empty() {
            return None;
        }
        self.by_id.get(wanted).copied().or_else(|| {
            let key = search_key(wanted);
            self.by_name.get(&key).copied().filter(|_| !key.is_empty())
        })
    }

    fn refuse(&mut self) {
        // Inclusions that are sound by themselves, as edges from an order to the order it
        // reads.
        let mut edges: Vec<(usize, usize, usize)> = Vec::new();
        for (at, order) in self.orders.iter().enumerate() {
            for (step_at, step) in order.steps.iter().enumerate() {
                let Some(include) = step.include.as_deref() else {
                    continue;
                };
                if carries_more_than_include(step) {
                    self.refused.insert(
                        (at, step_at),
                        "an include carries nothing else: no work, unit, edition or range"
                            .to_string(),
                    );
                } else if let Some(target) = self.targets[at][step_at] {
                    edges.push((at, step_at, target));
                } else {
                    self.refused.insert(
                        (at, step_at),
                        format!("\"{}\" is not an order of this universe", include.trim()),
                    );
                }
            }
        }

        let mut adjacent = vec![Vec::new(); self.orders.len()];
        for &(from, _, to) in &edges {
            adjacent[from].push(to);
        }
        let component = components(&adjacent);
        for &(from, step_at, target) in &edges {
            // Two orders in one strongly connected component read each other; an order
            // that includes itself is its own component and is caught by the same test.
            if component[from] == component[target] {
                self.refused.insert(
                    (from, step_at),
                    format!(
                        "including \"{}\" closes a loop",
                        declared_id(&self.orders[target], target)
                    ),
                );
            }
        }
    }

    /// The steps of `orders[at]` with each inclusion replaced by the steps it designates
    /// and each refused one skipped, paid for out of `budget`.
    ///
    /// Flattened at scan time and not when read, because the rows are derived and rewritten
    /// whole at every scan: « Tout Aquilon » follows « Terres d'Arran » the next time
    /// anything is scanned, with no second truth to keep in step.
    ///
    /// Walked with a stack of its own and not by recursion: a chain of ten thousand orders,
    /// each including the next, is a file a few hundred kilobytes long and would otherwise
    /// be ten thousand frames deep.
    pub fn flatten(&self, at: usize, budget: &mut Budget) -> Result<Vec<FlatStep<'a>>, Over> {
        let mut out: Vec<FlatStep<'a>> = Vec::new();
        let mut looked_at = 0usize;
        // (order, next step to look at, whether the order is the one being flattened)
        let mut stack = vec![(at, 0usize, true)];
        while let Some(frame) = stack.last_mut() {
            let (order, step_at, written_here) = *frame;
            let Some(step) = self.orders[order].steps.get(step_at) else {
                stack.pop();
                continue;
            };
            frame.1 += 1;

            looked_at += 1;
            budget.looked_left = budget.looked_left.checked_sub(1).ok_or(Over::Universe)?;
            if looked_at > MOST_STEPS_LOOKED_AT {
                return Err(Over::Order);
            }
            if self.refused.contains_key(&(order, step_at)) {
                continue;
            }
            match self.targets[order][step_at] {
                Some(target) if step.include.is_some() => stack.push((target, 0, false)),
                _ => {
                    if out.len() >= MOST_FLAT_STEPS {
                        return Err(Over::Order);
                    }
                    out.push(FlatStep {
                        step,
                        own: written_here.then_some(step_at),
                    });
                }
            }
        }
        budget.flat_left = budget
            .flat_left
            .checked_sub(out.len())
            .ok_or(Over::Universe)?;
        Ok(out)
    }
}

/// Which ceiling an order ran into.
#[derive(Debug, PartialEq, Eq)]
pub enum Over {
    /// Past `MOST_FLAT_STEPS` on its own.
    Order,
    /// The universe's orders, together, are past what a universe may hold.
    Universe,
}

/// What is left of a universe's allowance, shared by all its orders.
pub struct Budget {
    flat_left: usize,
    looked_left: usize,
}

impl Budget {
    pub fn new() -> Self {
        Budget {
            flat_left: MOST_UNIVERSE_STEPS,
            looked_left: MOST_UNIVERSE_LOOKS,
        }
    }
}

impl Default for Budget {
    fn default() -> Self {
        Self::new()
    }
}

/// A step of a flattened order. `own` is the step's number in the order that writes it
/// (counted from 0), and `None` for a step that came from an included order — the order
/// that writes it already complains about it.
pub struct FlatStep<'a> {
    pub step: &'a StepJson,
    pub own: Option<usize>,
}

/// Strongly connected components of a graph, numbered; two nodes share a number exactly
/// when each can reach the other. Tarjan's algorithm, kept off the call stack for the same
/// reason as the walk above.
fn components(adjacent: &[Vec<usize>]) -> Vec<usize> {
    const UNSEEN: usize = usize::MAX;
    let n = adjacent.len();
    let mut index = vec![UNSEEN; n];
    let mut low = vec![0usize; n];
    let mut on_stack = vec![false; n];
    let mut component = vec![UNSEEN; n];
    let mut open: Vec<usize> = Vec::new();
    let (mut next_index, mut next_component) = (0usize, 0usize);

    for root in 0..n {
        if index[root] != UNSEEN {
            continue;
        }
        let mut work = vec![(root, 0usize)];
        while let Some(&(node, child)) = work.last() {
            if child == 0 && index[node] == UNSEEN {
                index[node] = next_index;
                low[node] = next_index;
                next_index += 1;
                open.push(node);
                on_stack[node] = true;
            }
            if let Some(&to) = adjacent[node].get(child) {
                if let Some(top) = work.last_mut() {
                    top.1 += 1;
                }
                if index[to] == UNSEEN {
                    work.push((to, 0));
                } else if on_stack[to] {
                    low[node] = low[node].min(index[to]);
                }
                continue;
            }
            work.pop();
            if let Some(&(parent, _)) = work.last() {
                low[parent] = low[parent].min(low[node]);
            }
            if low[node] == index[node] {
                close(
                    node,
                    next_component,
                    &mut open,
                    &mut on_stack,
                    &mut component,
                );
                next_component += 1;
            }
        }
    }
    component
}

/// Numbers every node still open down to `root`, which a component's members are.
fn close(
    root: usize,
    number: usize,
    open: &mut Vec<usize>,
    on_stack: &mut [bool],
    component: &mut [usize],
) {
    while let Some(member) = open.pop() {
        on_stack[member] = false;
        component[member] = number;
        if member == root {
            break;
        }
    }
}
