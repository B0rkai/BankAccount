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
  body: unknown // the error response as sent (an import's 409 says who is busy, or which session)
  constructor(status: number, message: string, body: unknown = null) {
    super(message)
    this.status = status
    this.body = body
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
    headers: init?.body && !(init.body instanceof FormData) ? { 'Content-Type': 'application/json', ...init.headers } : init?.headers,
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
    throw new ApiError(res.status, message, body)
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

// docs/changelog.json: what changed since the last version, then each version's changes.
export interface Changelog {
  unreleased?: string[]
  released?: { version: string; date: string; changes: string[] }[] // oldest first
}

export const api = {
  me: () => request<Me>('/api/v1/me'),
  changelog: () => request<Changelog>('/api/v1/changelog'),
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

// ---- imports (daemon/ImportApi.h) ----

export type ImportTopic = 'type' | 'client' | 'category'
export type How = 'auto' | 'manual' | 'recalled' | null

export interface ImportSummary {
  id: number
  state: 'categorizing' | 'review' | 'committed'
  account_id: number | null
  account: string
  new_account: boolean
  account_number: string
  bank: string
  currency: string
  source_name: string
  source_format: string
  from: string | null
  to: string | null
  rows: number
  open: number // rows still needing an answer
  dropped: number // cut off by Stop here
  stop_at: number | null
  first_stoppable: number // rows before this replace stored ones: Stop here can't drop them
  skipped: number // older than what's stored, left out
  replaced: number // stored transactions this import replaces on Save
  created_at: string
  created_by: string
  updated_at: string
  updated_by: string
}

// What an upload would start (preview) - the summary without id/state.
export interface ImportDetected extends Omit<ImportSummary, 'id' | 'state'> {
  file_rows: number
  new_rows: number
}

export interface ImportRow {
  idx: number
  date: string
  amount: number
  currency: string
  raw: { type: string; client: string; client_account: string; memo: string }
  type_id: number | null
  type: string | null
  client_id: number | null
  client: string | null
  category_id: number | null
  category: string | null
  description: string
  how: Record<ImportTopic, How>
}

export interface ImportQuestion {
  row: number
  topic: ImportTopic
  choices: { id: number; name: string }[]
  preselect: number | null
  create_suggestion: string
  exact_value: string
  optional: boolean // "Default" (no client / uncategorized) is allowed
}

export interface ImportDetails extends ImportSummary {
  open_rows: number[]
  question: ImportQuestion | null
  current: ImportRow | null
  next: ImportRow | null
  previous: (ImportRow | (TransactionRow & { history: true }))[] // near the start: the account's last stored ones
}

export interface ImportAnswer {
  row: number
  topic: ImportTopic
  kind: 'select' | 'create' | 'default'
  id?: number
  name?: string
  keyword?: string
  definitive?: boolean
  description?: string
}

export interface ImportRows {
  rows: ImportRow[]
  summary: { count: number; total: number; currency: string; replaced: number; open: number; dropped: number }
  session: ImportSummary
}

// Someone else changed the session within the last 10 minutes: their change is refused until "take over".
export const busyOf = (e: unknown) =>
  e instanceof ApiError && e.status === 409 ? (e.body as { busy?: { by: string; at: string } } | null)?.busy : undefined

const imp = (id: number, rest = '') => `/api/v1/imports/${id}${rest}`

export const imports = {
  list: () => request<{ imports: ImportSummary[] }>('/api/v1/imports').then((r) => r.imports),
  // preview: only report what was detected. A 409 ApiError's body has the open session_id.
  upload: (file: File, opts: { preview?: boolean; accountName?: string }) => {
    const form = new FormData()
    form.append('file', file)
    if (opts.preview) form.append('preview', '1')
    if (opts.accountName) form.append('account_name', opts.accountName)
    return request<ImportDetails & { detected?: ImportDetected }>('/api/v1/imports', { method: 'POST', body: form })
  },
  details: (id: number, row?: number) => request<ImportDetails>(imp(id, row === undefined ? '' : `?row=${row}`)),
  // Answers can create clients, categories and keywords: the rest of the app reloads.
  answer: (id: number, answer: ImportAnswer, takeOver = false) =>
    post<ImportDetails>(imp(id, '/answer'), { ...answer, take_over: takeOver }).then((r) => (answer.kind === 'create' || answer.keyword ? dataChanged(r) : r)),
  // row: where to cut (null keeps every row again); left out, the first open row.
  stop: (id: number, row: number | null | undefined, takeOver = false) =>
    post<ImportDetails>(imp(id, '/stop'), row === undefined ? { take_over: takeOver } : { row, take_over: takeOver }),
  rows: (id: number) => request<ImportRows>(imp(id, '/rows')),
  editRow: (id: number, idx: number, change: TransactionEdit, takeOver = false) =>
    request<{ ok: boolean; row: ImportRow }>(imp(id, `/rows/${idx}`), { method: 'PATCH', body: JSON.stringify({ ...change, take_over: takeOver }) }),
  commit: (id: number, takeOver = false) => post<{ ok: boolean; account_id: number; saved: number }>(imp(id, '/commit'), { take_over: takeOver }).then(dataChanged),
  discard: (id: number, takeOver = false) => request<{ ok: boolean }>(imp(id), { method: 'DELETE', body: JSON.stringify({ take_over: takeOver }) }),
}

// Names change with an edit or import here (dataChanged clears this).
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
