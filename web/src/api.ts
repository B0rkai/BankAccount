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
  can_edit?: boolean // editing is on on the server and this sign-in may change data (Tailscale)
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
  account_full: string // "Bank::Name": short names repeat across banks; what an accounts filter takes
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
  account_id: number // where it is: what an edit addresses
  seq: number
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
const put = <T>(path: string, body: unknown) => request<T>(path, { method: 'PUT', body: JSON.stringify(body) })

// The web app's settings, kept by the daemon so every browser shares them. Category ids.
export interface WebSettings {
  excluded_categories?: number[] | null // left out of the overview's totals
  other_income_categories?: number[] | null // money in under these isn't counted as income
}

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
  order?: 'largest' // by size, in HUF; newest first otherwise
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

// Every change made here bumps this: pages reload what they show, cached names are dropped.
let dataVersion = 0
const dataListeners = new Set<() => void>()
export function onDataChanged(listener: () => void): () => void {
  dataListeners.add(listener)
  return () => dataListeners.delete(listener)
}
export function getDataVersion() {
  return dataVersion
}
function dataChanged<T>(result: T): T {
  dataVersion++
  nameCache.clear()
  firstYear = undefined
  dataListeners.forEach((l) => l())
  return result
}

// Who is signed in, set once by the app shell.
let currentMe: Me | undefined
export function setMe(me: Me) {
  currentMe = me
}
export const canEdit = () => !!currentMe?.can_edit

export type EntityTopic = 'client' | 'category' | 'type'
export interface Keyword {
  keyword: string
  definitive: boolean // false: only a suggestion, never sorts on its own
}
export interface EntityDetails {
  id: number
  name: string
  keywords: Keyword[]
  account_numbers?: string[]
}
export interface TransactionEdit {
  category_id?: number
  client_id?: number
  description?: string
}
// The transaction to edit, as the list showed it - the server checks date and amount still match.
const address = (t: TransactionRow) => ({ account_id: t.account_id, seq: t.seq, date: t.date, amount: t.amount })

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
  settings: () => request<WebSettings>('/api/v1/settings'),
  saveSettings: (settings: WebSettings) => put<WebSettings>('/api/v1/settings', settings),
  deleteFavoriteReport: (name: string) => del(`/api/v1/favorites/reports?name=${encodeURIComponent(name)}`).then(favoritesChanged),

  entity: (topic: EntityTopic, id: number) => request<EntityDetails>(`/api/v1/entity?topic=${topic}&id=${id}`),
  editTransaction: (t: TransactionRow, change: TransactionEdit) =>
    request<{ transaction: TransactionRow }>('/api/v1/transactions', { method: 'PATCH', body: JSON.stringify({ ...address(t), ...change }) }).then(dataChanged),
  createEntity: (topic: 'client' | 'category', name: string) => post<{ id: number; name: string; created: boolean }>('/api/v1/entities', { topic, name }).then(dataChanged),
  renameEntity: (topic: EntityTopic | 'account', id: number, name: string) => post<{ changed: boolean }>('/api/v1/entities/rename', { topic, id, name }).then(dataChanged),
  addKeyword: (topic: EntityTopic, id: number, keyword: string, definitive: boolean) =>
    post<{ changed: boolean }>('/api/v1/entities/keywords', { topic, id, keyword, definitive }).then(dataChanged),
  removeKeyword: (topic: EntityTopic, id: number, k: Keyword) =>
    request<{ changed: boolean }>('/api/v1/entities/keywords', { method: 'DELETE', body: JSON.stringify({ topic, id, ...k }) }).then(dataChanged),
  // Responds with the target's id after the merge (ids after the removed ones shift down).
  merge: (topic: EntityTopic, from: number[], to: number) => post<{ id: number }>('/api/v1/entities/merge', { topic, from, to }).then(dataChanged),
  recategorizePreview: () => request<{ proposals: TransactionRow[] }>('/api/v1/recategorize'),
  recategorize: (items: TransactionRow[]) =>
    post<{ applied: number; skipped: number }>('/api/v1/recategorize', { items: items.map((t) => ({ account_id: t.account_id, seq: t.seq, category_id: t.category_id })) }).then(dataChanged),
  updateExchangeRates: () =>
    post<{ changed: boolean; message: string }>('/api/v1/exchange-rates/update', {}).then((r) => (r.changed ? dataChanged(r) : r)),
}

// Names change when the desktop app saves or an edit here does (dataChanged clears this).
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
