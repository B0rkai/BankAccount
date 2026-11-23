import { useEffect, useState } from 'react'

// A handful of pages don't need a routing library: the path picks the page, the query string
// carries a page's state (so a query in Explore can be bookmarked), and navigate() pushes history
// entries.

export type Route = 'overview' | 'explore' | 'transactions' | 'reports' | 'client' | 'year' | 'category' | 'settings' | 'imports' | 'import'

// `short`: the phone tab bar's label, where six tabs share the width.
export const ROUTES: { route: Route; path: string; label: string; short?: string }[] = [
  { route: 'overview', path: '/', label: 'Overview' },
  { route: 'explore', path: '/explore', label: 'Explore' },
  { route: 'transactions', path: '/transactions', label: 'Transactions', short: 'Activity' },
  { route: 'reports', path: '/reports', label: 'Reports' },
  { route: 'imports', path: '/imports', label: 'Import' }, // only for a sign-in that may edit
  { route: 'settings', path: '/settings', label: 'Settings' },
]

// Pages reached by links rather than the menu, and the menu entry they belong under.
export const SUB_ROUTES: { route: Route; path: string; parent: Route }[] = [
  { route: 'client', path: '/client', parent: 'transactions' },
  { route: 'year', path: '/year', parent: 'reports' },
  { route: 'category', path: '/category', parent: 'overview' },
  { route: 'import', path: '/import', parent: 'imports' },
]

export const menuRoute = (route: Route): Route => SUB_ROUTES.find((r) => r.route === route)?.parent ?? route

function current(): { route: Route; params: URLSearchParams } {
  const match = [...ROUTES, ...SUB_ROUTES].find((r) => r.path === window.location.pathname)
  return { route: match?.route ?? 'overview', params: new URLSearchParams(window.location.search) }
}

export function navigate(path: string, opts: { replace?: boolean } = {}) {
  if (opts.replace) window.history.replaceState(null, '', path)
  else window.history.pushState(null, '', path)
  window.dispatchEvent(new PopStateEvent('popstate'))
  if (!opts.replace) window.scrollTo(0, 0)
}

export function useRoute() {
  const [state, setState] = useState(current)
  useEffect(() => {
    const onPop = () => setState(current())
    window.addEventListener('popstate', onPop)
    return () => window.removeEventListener('popstate', onPop)
  }, [])
  return state
}

// A plain <a> that navigates client-side (and still opens in a new tab on ctrl/middle click).
export function linkHandler(path: string) {
  return (e: React.MouseEvent) => {
    if (e.defaultPrevented || e.button !== 0 || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) return
    e.preventDefault()
    navigate(path)
  }
}
