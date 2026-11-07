use std::ops::Deref;
use std::sync::Arc;

/// A statically routed Moss domain capability. The runtime wrapper holds the
/// generated, domain-specific capability payload behind shared ownership.
pub struct DomainHandle<T> {
    inner: Arc<T>,
}

impl<T> DomainHandle<T> {
    pub fn new(inner: T) -> Self {
        Self {
            inner: Arc::new(inner),
        }
    }
}

impl<T> Clone for DomainHandle<T> {
    fn clone(&self) -> Self {
        Self {
            inner: Arc::clone(&self.inner),
        }
    }
}

impl<T> Deref for DomainHandle<T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

/// Shares a statically typed domain capability. Moss values cannot enter this
/// API because the argument must be a `DomainHandle<T>`.
#[inline]
pub fn share_domain<T>(handle: &DomainHandle<T>) -> DomainHandle<T> {
    handle.clone()
}
