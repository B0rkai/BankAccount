import { useEffect, useState } from 'react'

// Four pages don't need a routing library: the path picks the page, the query string carries a
// page's state (so a query in Explore can be bookmarked), and navigate() pushes history entries.

export type Route = 'overview' | 'explore' | 'transactions' | 'reports'

export const ROUTES: { route: Route; path: string; label: string }[] = [
  { route: 'overview', path: '/', label: 'Overview' },
  { route: 'explore', path: '/explore', label: 'Explore' },
  { route: 'transactions', path: '/transactions', label: 'Transactions' },
  { route: 'reports', path: '/reports', label: 'Reports' },
]

function current(): { route: Route; params: URLSearchParams } {
  const match = ROUTES.find((r) => r.path === window.location.pathname)
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
