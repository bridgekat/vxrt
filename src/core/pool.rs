//! Fixed-size slots with stable indices, doubling growth, and feedback invalidation logs.
use std::collections::HashSet;
/// A slot index, valid only while its allocation remains live.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Handle {
    pub index: u32,
}
pub struct Pool<T> {
    slots: Vec<Option<T>>,
    free: Vec<u32>,
    len: usize,
    limit: usize,
    // Changes since the state that produced the outstanding GPU feedback batch.
    // This log is independent of upload synchronization.
    feedback_invalidated: Option<HashSet<u32>>,
}
impl<T> Pool<T> {
    pub fn new(initial: usize, limit: usize) -> Self {
        assert!(
            initial > 0
                && initial.is_power_of_two()
                && initial <= limit
                && limit <= u32::MAX as usize
        );
        let mut pool = Self {
            slots: Vec::new(),
            free: Vec::new(),
            len: 0,
            limit,
            feedback_invalidated: None,
        };
        pool.grow(initial);
        pool
    }
    fn grow(&mut self, capacity: usize) {
        let old = self.slots.len();
        self.slots.resize_with(capacity, || None);
        self.free.extend((old as u32..capacity as u32).rev());
    }
    pub fn can_allocate(&self, count: usize) -> bool {
        let required = self.len.saturating_add(count);
        required <= self.capacity()
            || required
                .checked_next_power_of_two()
                .is_some_and(|n| n <= self.limit)
    }
    pub fn allocate(&mut self, value: T) -> Option<Handle> {
        if self.free.is_empty() {
            let size = self.capacity().checked_mul(2)?;
            if size > self.limit {
                return None;
            }
            self.grow(size);
        }
        let index = self.free.pop()?;
        self.invalidate(index);
        self.slots[index as usize] = Some(value);
        self.len += 1;
        Some(Handle { index })
    }
    pub fn get(&self, h: Handle) -> Option<&T> {
        self.slots.get(h.index as usize)?.as_ref()
    }
    pub fn at(&self, index: u32) -> Option<(Handle, &T)> {
        let h = Handle { index };
        Some((h, self.get(h)?))
    }
    pub fn get_mut(&mut self, h: Handle) -> Option<&mut T> {
        self.slots.get_mut(h.index as usize)?.as_mut()
    }
    pub fn remove(&mut self, h: Handle) -> Option<T> {
        let value = self.slots.get_mut(h.index as usize)?.take()?;
        self.invalidate(h.index);
        self.free.push(h.index);
        self.len -= 1;
        Some(value)
    }
    fn invalidate(&mut self, index: u32) {
        if let Some(log) = &mut self.feedback_invalidated {
            log.insert(index);
        }
    }
    /// Begin tracking the GPU state whose feedback is about to be captured.
    /// Call after recording uploads and before any further CPU pool mutations.
    pub fn begin_feedback(&mut self) {
        assert!(
            self.feedback_invalidated.is_none(),
            "feedback batch already outstanding"
        );
        self.feedback_invalidated = Some(HashSet::new());
    }
    /// True only if the slot still identifies its allocation from that GPU state.
    /// Contents may have changed in place; callers must recheck operation eligibility.
    pub fn accepts_feedback(&self, h: Handle) -> bool {
        self.feedback_invalidated
            .as_ref()
            .is_some_and(|log| !log.contains(&h.index))
            && self.get(h).is_some()
    }
    /// Clear only after the corresponding batch is consumed or discarded, never at upload sync.
    pub fn finish_feedback(&mut self) {
        self.feedback_invalidated = None;
    }
    pub fn capacity(&self) -> usize {
        self.slots.len()
    }
    pub fn len(&self) -> usize {
        self.len
    }
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }
    pub fn iter(&self) -> impl Iterator<Item = (Handle, &T)> {
        self.slots
            .iter()
            .enumerate()
            .filter_map(|(i, s)| s.as_ref().map(|v| (Handle { index: i as u32 }, v)))
    }
}
