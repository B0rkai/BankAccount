// The daemon's JSON API (daemon/main.cpp, /api/v1). Shapes mirror QueryApi.h, BrowseApi.h and
// FavoritesApi.h on the C++ side.

export interface Me {
  version: string
  via: 'tailscale' | 'lan'
  login: string
  name: string
  db_loaded: boolean
  last_loaded: string
  accounts?: number
  transactions?: number
  last_record_date?: string
}

// One query, as POST /api/v1/query takes it - the favorite_queries.json field set minus "name"
// (FavoriteQuery.h's ParseAdHocQuery).
export interface QueryDef {
  accounts?: string[]
  clients?: string[]
  categories?: string[]
  types?: string[]
  exclude_clients?: boolean
  exclude_categories?: boolean
  exclude_types?: boolean
  relative_period?: string
  date_from?: string
  date_to?: string
  aggregate_by?: string[]
  period?: string
  show_list?: boolean
}

export interface FavoriteQuery extends QueryDef {
  name: string
  chart?: { kind?: string; side?: string }
}

export interface FavoriteReport {
  name: string
  favorite_query: string
  chart_kinds?: string[]
  chart_sides?: string[]
}

export interface Table {
  header: string[]
  align: ('left' | 'right')[]
  rows: string[][]
}

export interface ChartData {
  currency: string
  labels: string[]
  series: { name: string; values: number[] }[]
}

export type DatasetKey = 'net' | 'income' | 'expense'

export interface ChartDataset {
  key: DatasetKey
  label: string
  allowed_kinds: string[]
  colour: string
  default_currency: string
  native: ChartData[]
  // Every currency exchanged into one and merged - only present with more than one currency.
  converted: ChartData[]
}

export interface ChartResult {
  period_unit: string
  others_colour: string
  palette: string[]
  datasets: ChartDataset[]
}

export interface Section {
  heading: string
  table: Table
  chart_shape?: 'topic_sum' | 'periodic'
  chart?: ChartResult
}

export interface TransactionRow {
  account: string
  date: string
  type: string
  client: string
  memo: string
  description: string
  category: string
  amount: number
  currency: string
  display: string
}

export interface TransactionPage {
  total: number
  offset: number
  transactions: TransactionRow[]
}

export class ApiError extends Error {
  status: number
  constructor(status: number, message: string) {
    super(message)
    this.status = status
  }
}

// Every API failure but 401 surfaces as an error message; 401 (LAN listener, not signed in yet)
// tells the app shell to show the sign-in page instead.
const unauthorizedListeners = new Set<() => void>()
export function onUnauthorized(listener: () => void): () => void {
  unauthorizedListeners.add(listener)
  return () => unauthorizedListeners.delete(listener)
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(path, {
    ...init,
    headers: init?.body ? { 'Content-Type': 'application/json', ...init.headers } : init?.headers,
    credentials: 'same-origin',
  })
  const text = await res.text()
  let body: unknown = null
  try {
    body = text ? JSON.parse(text) : null
  } catch {
    // not JSON - fall through to the status check
  }
  if (!res.ok) {
    if (res.status === 401) unauthorizedListeners.forEach((l) => l())
    const message = (body as { error?: string } | null)?.error ?? `${res.status} ${res.statusText}`
    throw new ApiError(res.status, message)
  }
  return body as T
}

const post = <T>(path: string, body: unknown) => request<T>(path, { method: 'POST', body: JSON.stringify(body) })

export const api = {
  me: () => request<Me>('/api/v1/me'),
  login: (password: string, remember: boolean) => post<{ ok: boolean }>('/api/v1/login', { password, remember }),
  logout: () => post<{ ok: boolean }>('/api/v1/logout', {}),
  names: (topic: 'accounts' | 'categories' | 'clients') =>
    request<{ names: string[] }>(`/api/v1/names?topic=${topic}`).then((r) => r.names),
  query: (def: QueryDef) => post<Section[]>('/api/v1/query', def),
  transactions: (def: QueryDef & { limit?: number; offset?: number }) => post<TransactionPage>('/api/v1/transactions', def),
  favoriteQueries: () => request<FavoriteQuery[]>('/api/v1/favorites/queries'),
  favoriteReports: () => request<FavoriteReport[]>('/api/v1/favorites/reports'),
  runFavorite: (name: string) => request<Section[]>(`/api/v1/favorites/queries/run?name=${encodeURIComponent(name)}`),
  reportHtmlUrl: (name: string) => `/api/v1/favorites/reports/html?name=${encodeURIComponent(name)}`,
}

// Names change only when the desktop app saves; one fetch per page load is plenty.
const nameCache = new Map<string, Promise<string[]>>()
export function cachedNames(topic: 'accounts' | 'categories' | 'clients'): Promise<string[]> {
  let p = nameCache.get(topic)
  if (!p) {
    p = api.names(topic).catch((e) => {
      nameCache.delete(topic)
      throw e
    })
    nameCache.set(topic, p)
  }
  return p
}
