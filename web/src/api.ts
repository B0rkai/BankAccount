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
  client_id: number
  memo: string
  description: string
  category: string
  category_id: number
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
const del = <T>(path: string) => request<T>(path, { method: 'DELETE' })

export type NameTopic = 'accounts' | 'categories' | 'clients'
export interface NameList {
  names: string[]
  ids?: number[] // categories and clients: the id behind each name (filter on "#<id>" to match exactly)
}

export interface TransactionQuery extends QueryDef {
  limit?: number
  offset?: number
  search?: string
  direction?: 'in' | 'out'
}

// Favorites change when saved or deleted here; pages showing them listen for saves.
let favoritesVersion = 0
const favoritesListeners = new Set<() => void>()
export function onFavoritesChanged(listener: () => void): () => void {
  favoritesListeners.add(listener)
  return () => favoritesListeners.delete(listener)
}
export function getFavoritesVersion() {
  return favoritesVersion
}
function favoritesChanged<T>(result: T): T {
  favoritesVersion++
  favoritesListeners.forEach((l) => l())
  return result
}

export const api = {
  me: () => request<Me>('/api/v1/me'),
  login: (password: string, remember: boolean) => post<{ ok: boolean }>('/api/v1/login', { password, remember }),
  logout: () => post<{ ok: boolean }>('/api/v1/logout', {}),
  names: (topic: NameTopic) => request<NameList>(`/api/v1/names?topic=${topic}`),
  query: (def: QueryDef) => post<Section[]>('/api/v1/query', def),
  transactions: (def: TransactionQuery) => post<TransactionPage>('/api/v1/transactions', def),
  favoriteQueries: () => request<FavoriteQuery[]>('/api/v1/favorites/queries'),
  favoriteReports: () => request<FavoriteReport[]>('/api/v1/favorites/reports'),
  runFavorite: (name: string) => request<Section[]>(`/api/v1/favorites/queries/run?name=${encodeURIComponent(name)}`),
  reportHtmlUrl: (name: string) => `/api/v1/favorites/reports/html?name=${encodeURIComponent(name)}`,
  // A 409 ApiError means the name is taken: ask, then send again with replace.
  saveFavoriteQuery: (fav: FavoriteQuery, replace = false) =>
    post<FavoriteQuery>('/api/v1/favorites/queries', { ...fav, replace }).then(favoritesChanged),
  saveFavoriteReport: (report: FavoriteReport, replace = false) =>
    post<FavoriteReport>('/api/v1/favorites/reports', { ...report, replace }).then(favoritesChanged),
  deleteFavoriteQuery: (name: string) => del(`/api/v1/favorites/queries?name=${encodeURIComponent(name)}`).then(favoritesChanged),
  deleteFavoriteReport: (name: string) => del(`/api/v1/favorites/reports?name=${encodeURIComponent(name)}`).then(favoritesChanged),
}

// Names change only when the desktop app saves; one fetch per page load is plenty.
const nameCache = new Map<string, Promise<NameList>>()
export function cachedNameList(topic: NameTopic): Promise<NameList> {
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
export const cachedNames = (topic: NameTopic) => cachedNameList(topic).then((l) => l.names)

// The year of the oldest transaction, for the year-in-review picker.
let firstYear: Promise<number> | undefined
export function cachedFirstYear(): Promise<number> {
  firstYear ??= api
    .transactions({ limit: 1 })
    .then((newest) => api.transactions({ limit: 1, offset: Math.max(0, newest.total - 1) }))
    .then((oldest) => parseInt(oldest.transactions[0]?.date.slice(0, 4) ?? '', 10) || new Date().getFullYear())
    .catch((e) => {
      firstYear = undefined
      throw e
    })
  return firstYear
}
