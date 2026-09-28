import { useCallback, useEffect, useRef, useState } from 'react'
import { Alert, Button, Checkbox, Input, Modal, Select, Space, Spin, Table, Tabs, Typography } from 'antd'
import { correctMedia, executeOrganization, getInbox, getSettings, previewOrganization, searchBangumi } from '../api/client'
import type { BangumiSearch, InboxOrigin, Media, Preferences, Preview } from '../api/types'

type Props = { onOpenAnime: (id: number) => void }
type Correction = Pick<Media, 'title' | 'season' | 'episodeNumber' | 'episodeType'>
const errorText = (cause: unknown) => cause instanceof Error ? cause.message : '请求失败'
const episodeTypes = ['normal', 'sp', 'ova', 'ncop', 'nced']
const executable = (plan: Preview | null) => !!plan && plan.conflicts.length === 0 && Number.isFinite(Date.parse(plan.expiresAt)) && Date.parse(plan.expiresAt) > Date.now()

export function InboxPage({ onOpenAnime }: Props) {
  // 分来源保留分页位置，避免在 qB 与外来导入标签间切换时互相覆盖列表状态。
  const [items, setItems] = useState<Media[]>([])
  const [origin, setOrigin] = useState<InboxOrigin>('qb_download')
  const [offsets, setOffsets] = useState<Record<InboxOrigin, number>>({ qb_download: 0, external_import: 0 })
  const offset = offsets[origin]
  const [nextOffset, setNextOffset] = useState<number | null>(null)
  const [total, setTotal] = useState(0)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const [selected, setSelected] = useState<Media | null>(null)
  const [correction, setCorrection] = useState<Correction | null>(null)
  const [query, setQuery] = useState('')
  const [results, setResults] = useState<BangumiSearch | null>(null)
  const [operation, setOperation] = useState<Preferences['preferredOperation']>('hardlink')
  const [plan, setPlan] = useState<Preview | null>(null)
  const [key, setKey] = useState('')
  const [confirmOpen, setConfirmOpen] = useState(false)
  const [qbComplete, setQbComplete] = useState(false)
  const [busy, setBusy] = useState(false)
  const [retry, setRetry] = useState(false)
  // 序号令牌用于丢弃过期的异步搜索/预览结果，防止覆盖用户的新选择。
  const previewSequence = useRef(0)
  const selectionSequence = useRef(0)
  const searchSequence = useRef(0)
  const operationTouched = useRef(false)
  const draftChanged = !!selected && !!correction && (correction.title !== selected.title || correction.season !== selected.season || correction.episodeNumber !== selected.episodeNumber || correction.episodeType !== selected.episodeType)

  const refresh = useCallback(async (signal?: AbortSignal) => {
    setLoading(true)
    try { const page = await getInbox(100, offset, signal, origin); if (signal?.aborted) return; setItems(page.items); setNextOffset(page.nextOffset); setTotal(page.total); setError('') }
    catch (cause) { if (!signal?.aborted) setError(errorText(cause)) }
    finally { if (!signal?.aborted) setLoading(false) }
  }, [offset, origin])
  useEffect(() => { const controller = new AbortController(); void refresh(controller.signal); return () => controller.abort() }, [refresh])
  useEffect(() => {
    let active = true
    void getSettings().then((settings) => { if (active && !operationTouched.current) setOperation(settings.preferredOperation) }).catch(() => { /* hardlink remains the safe local default */ })
    return () => { active = false }
  }, [])

  const clearPlan = () => { previewSequence.current++; setPlan(null); setKey(''); setQbComplete(false); setConfirmOpen(false); setRetry(false) }
  const choose = (item: Media) => { selectionSequence.current++; searchSequence.current++; clearPlan(); setSelected(item); setCorrection({ title: item.title, season: item.season, episodeNumber: item.episodeNumber, episodeType: item.episodeType }); setQuery(''); setResults(null); setError('') }
  const editCorrection = (field: keyof Correction, value: string) => { if (!correction) return; clearPlan(); setCorrection({ ...correction, [field]: value }) }
  const changePage = (next: number) => { selectionSequence.current++; searchSequence.current++; clearPlan(); setSelected(null); setCorrection(null); setOffsets((old) => ({ ...old, [origin]: next })) }
  const changeOrigin = (next: InboxOrigin) => { if (next === origin) return; selectionSequence.current++; searchSequence.current++; clearPlan(); setSelected(null); setCorrection(null); setItems([]); setNextOffset(null); setTotal(0); setError(''); setOrigin(next) }
  async function save() {
    if (!selected || !correction || !episodeTypes.includes(correction.episodeType)) return
    const sequence = selectionSequence.current
    clearPlan()
    setBusy(true); setError('')
    try { const updated = await correctMedia(selected.id, correction); if (sequence === selectionSequence.current) setSelected(updated); setItems((old) => old.map((item) => item.id === updated.id ? updated : item)) }
    catch (cause) { if (sequence === selectionSequence.current) setError(errorText(cause)) }
    finally { setBusy(false) }
  }
  async function search() {
    const value = query.trim()
    const bytes = new TextEncoder().encode(value).length
    if (bytes < 1 || bytes > 100) { setError('Bangumi 关键词须为 1–100 个 UTF-8 字节'); return }
    const sequence = ++searchSequence.current
    setBusy(true); setError(''); setResults(null)
    try { const found = await searchBangumi(value); if (sequence === searchSequence.current) setResults(found) }
    catch (cause) { if (sequence === searchSequence.current) setError(errorText(cause)) }
    finally { setBusy(false) }
  }
  async function preview() {
    if (!selected || draftChanged) return
    operationTouched.current = true
    clearPlan()
    const sequence = previewSequence.current
    setBusy(true); setError('')
    try { const next = await previewOrganization(selected.id, operation); if (sequence === previewSequence.current) { setPlan(next); setKey(`plan-${next.id}-${crypto.randomUUID()}`) } }
    catch (cause) { setError(errorText(cause)) }
    finally { setBusy(false) }
  }
  async function execute() {
    if (!plan || !selected || !executable(plan) || (selected.origin === 'qb_download' && !qbComplete)) return
    setBusy(true); setError('')
    try { await executeOrganization(plan.id, key, selected.origin === 'qb_download' && qbComplete); clearPlan(); await refresh() }
    catch (cause) { setError(errorText(cause)); setRetry(true) }
    finally { setBusy(false) }
  }

  return <Space direction="vertical" size="middle" style={{ width: '100%' }}>
    <Tabs activeKey={origin} onChange={(value) => changeOrigin(value as InboxOrigin)} items={[{ key: 'qb_download', label: 'qB 下载' }, { key: 'external_import', label: '外来导入' }]} />
    <Button onClick={() => void refresh()} disabled={loading}>刷新</Button>
    {loading && <Spin aria-label="正在加载待整理文件" />}
    {error && !confirmOpen && <Alert type="error" showIcon message={error} />}
    <Table rowKey="id" loading={loading} dataSource={items} locale={{ emptyText: '暂无待整理文件' }} pagination={false} columns={[
      { title: '文件名', dataIndex: 'filename' }, { title: '状态', dataIndex: 'status' },
      { title: '置信度', dataIndex: 'confidence' }, { title: '标题', dataIndex: 'title' },
      { title: '季', dataIndex: 'season' }, { title: '集数', dataIndex: 'episodeNumber' },
      { title: '操作', render: (_, item: Media) => <Button onClick={() => choose(item)} aria-label={`编辑 ${item.title}`}>编辑与整理</Button> },
    ]} />
    <Space><Typography.Text>共 {total} 条</Typography.Text><Button disabled={loading || offset === 0} onClick={() => changePage(Math.max(0, offset - 100))}>上一页</Button><Button disabled={loading || nextOffset === null} onClick={() => changePage(nextOffset!)}>下一页</Button></Space>
    {selected && correction && <Space direction="vertical" style={{ width: '100%' }}>
      <Typography.Title level={5}>{selected.filename}</Typography.Title>
      <Typography.Text>识别标题：{selected.parsedTitle}</Typography.Text>
      <Input aria-label="规范标题" value={correction.title} onChange={(event) => editCorrection('title', event.target.value)} />
      <Input aria-label="季" value={correction.season} onChange={(event) => editCorrection('season', event.target.value)} />
      <Input aria-label="集数" value={correction.episodeNumber} onChange={(event) => editCorrection('episodeNumber', event.target.value)} />
      <Select aria-label="集数类型" placeholder="请选择集数类型" value={episodeTypes.includes(correction.episodeType) ? correction.episodeType : undefined} options={episodeTypes.map((value) => ({ value, label: value }))} onChange={(value) => editCorrection('episodeType', value)} />
      <Space><Button onClick={() => void save()} loading={busy} disabled={!episodeTypes.includes(correction.episodeType)}>保存修正</Button>{selected.animeId && <Button onClick={() => onOpenAnime(selected.animeId!)}>查看本地番剧</Button>}</Space>
      <Space.Compact><Input aria-label="Bangumi 关键词" value={query} onChange={(event) => setQuery(event.target.value)} /><Button onClick={() => void search()} loading={busy}>搜索 Bangumi</Button></Space.Compact>
      {results && <><Typography.Text>本地匹配：{results.localMatch?.displayTitle ?? '无'}</Typography.Text>{results.items.map((item, index) => <Typography.Text key={item.id}>{index + 1}. {item.nameCn || item.name}（{item.score}）</Typography.Text>)}</>}
      <Space><Select aria-label="整理方式" value={operation} options={['hardlink', 'copy', 'symlink'].map((value) => ({ value, label: value }))} onChange={(value) => { operationTouched.current = true; setOperation(value); clearPlan() }} /><Button onClick={() => void preview()} loading={busy} disabled={draftChanged}>生成预览</Button></Space>
      {draftChanged && <Typography.Text type="secondary">请先保存修正再生成预览</Typography.Text>}
      {plan && <Space direction="vertical"><Typography.Text>目标：{plan.targetPath}</Typography.Text><Typography.Text>方式：{plan.operation}；有效期：{plan.expiresAt}</Typography.Text>{plan.conflicts.map((conflict) => <Alert key={conflict} type="warning" message={conflict} />)}<Button disabled={!executable(plan)} onClick={() => setConfirmOpen(true)}>确认执行</Button></Space>}
    </Space>}
    <Modal title="确认整理" open={confirmOpen} onCancel={() => setConfirmOpen(false)} footer={null}>
      <Typography.Paragraph>来源：{selected?.origin}</Typography.Paragraph><Typography.Paragraph>目标：{plan?.targetPath}</Typography.Paragraph>
      {selected?.origin === 'qb_download' && <Checkbox checked={qbComplete} onChange={(event) => setQbComplete(event.target.checked)}>我确认 qB 下载已完成</Checkbox>}
      {error && <Alert type="error" message={error} />}
      <Button type="primary" disabled={busy || !executable(plan) || (selected?.origin === 'qb_download' && !qbComplete)} onClick={() => void execute()}>{retry ? '重试执行' : '执行整理'}</Button>
    </Modal>
  </Space>
}
