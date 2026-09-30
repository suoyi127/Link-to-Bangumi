import { useCallback, useEffect, useRef, useState } from 'react'
import { Alert, Button, Card, Input, List, Popconfirm, Space, Spin, Table, Tag, Typography } from 'antd'
import { editGame, getGames, importGame, launchGame, removeUnavailableGame, scrapeGame, scrapeGameVndb, searchGameBangumi, searchGameVndb } from '../api/client'
import type { GameCandidate, GameResource, GameScrape, VndbCandidate } from '../api/types'

const errorText = (error: unknown) => error instanceof Error ? error.message : '请求失败'
function scrapeMessage(result: GameScrape) {
  const source = result.source === 'vndb' ? 'VNDB' : 'Bangumi'
  if (result.bound) return result.coverUpdated ? `${source} 绑定与封面刮削完成。` : `已绑定 ${source}，封面暂未更新（${result.errorCode}）。`
  if (result.errorCode === 'game_match_requires_confirmation' || result.errorCode === 'vndb_match_requires_confirmation') return '游戏已登记，暂未找到唯一匹配；请在资源库 → 游戏资源中搜索或绑定条目。'
  return `游戏已登记，刮削暂未完成（${result.errorCode}）；可在游戏资源中重新刮削。`
}
function useGames() {
  const [items, setItems] = useState<GameResource[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const generation = useRef(0)
  const load = useCallback(async (signal?: AbortSignal, quiet = false) => {
    const current = ++generation.current
    if (!quiet) { setLoading(true); setError('') }
    try { const result = await getGames(signal); if (!signal?.aborted && current === generation.current) setItems(result.items) }
    catch (error) { if (!signal?.aborted && current === generation.current && !quiet) setError(errorText(error)) }
    finally { if (!signal?.aborted && current === generation.current) setLoading(false) }
  }, [])
  useEffect(() => {
    const requestGeneration = generation
    let controller = new AbortController()
    void load(controller.signal)
    // 只核对已登记启动文件；后台刷新不覆盖资源编辑中的草稿。
    const refresh = () => {
      if (document.visibilityState === 'hidden') return
      controller.abort(); controller = new AbortController(); void load(controller.signal, true)
    }
    const timer = window.setInterval(refresh, 5000)
    window.addEventListener('focus', refresh)
    return () => { controller.abort(); requestGeneration.current++; window.clearInterval(timer); window.removeEventListener('focus', refresh) }
  }, [load])
  return { items, setItems, loading, error, setError, load }
}
function GameCover({ game }: { game: GameResource }) {
  const [failedUrl, setFailedUrl] = useState('')
  return <div className="anime-cover" style={{ position: 'relative', width: '100%', aspectRatio: '2 / 3', overflow: 'hidden', background: '#edf2f4', display: 'grid', placeItems: 'center', color: '#83959e' }}>
    {game.coverUrl && failedUrl !== game.coverUrl ? <img src={game.coverUrl} alt={`${game.title} 封面`} style={{ position: 'absolute', inset: 0, width: '100%', height: '100%', objectFit: 'cover' }} onError={() => setFailedUrl(game.coverUrl)} /> : <span>暂无封面</span>}
  </div>
}
export function GameLibraryPage() {
  const { items, loading, error, setError, load } = useGames()
  const [selected, setSelected] = useState<number | null>(null)
  const [query, setQuery] = useState('')
  const [path, setPath] = useState('')
  const [busy, setBusy] = useState(false)
  const [message, setMessage] = useState('')
  const game = items.find((item) => item.id === selected)
  async function add() {
    if (busy) return
    setBusy(true); setError(''); setMessage('')
    try { const result = await importGame(path.trim()); setPath(''); setMessage(scrapeMessage(result)); await load() }
    catch (error) { setError(errorText(error)) } finally { setBusy(false) }
  }
  async function launch(id: number) {
    if (busy) return
    setBusy(true); setError(''); setMessage('')
    try { await launchGame(id); setMessage('已启动游戏。') } catch (error) { setError(errorText(error)) } finally { setBusy(false) }
  }
  return <Space direction="vertical" size="large" style={{ width: '100%' }}>
    <details className="settings-section">
      <summary style={{ cursor: 'pointer', fontSize: 16, fontWeight: 600 }}>导入游戏</summary>
      <Space direction="vertical" style={{ width: '100%', marginTop: 16 }}>
      <Typography.Text type="secondary">填写本机游戏 EXE 的完整路径，优先获取 Bangumi 游戏信息，未匹配时尝试 VNDB。</Typography.Text>
      <Space.Compact style={{ width: '100%' }}><Input aria-label="游戏可执行文件路径" value={path} maxLength={4096} disabled={busy} onChange={(event) => setPath(event.target.value)} placeholder="例如 D:\Games\游戏名称\Game.exe" /><Button type="primary" loading={busy} disabled={!path.trim()} onClick={() => void add()}>导入游戏</Button></Space.Compact>
    </Space></details>
    {error && <Alert type="error" message={error} />}{message && <Alert type="info" message={message} />}
    <Space wrap>{game && <Button onClick={() => setSelected(null)}>返回游戏库</Button>}<Button disabled={busy} loading={loading} onClick={() => void load()}>刷新游戏库</Button></Space>
    {loading ? <Spin aria-label="正在加载游戏库" /> : game ? <section className="settings-section">
      <div className="novel-detail-header"><GameCover game={game} /><div><Typography.Title level={3}>{game.title}</Typography.Title><Typography.Paragraph>{game.developer || '开发商待刮削'}</Typography.Paragraph>{game.platform && <Tag>{game.platform}</Tag>}<Space wrap>{game.subjectId && <a href={`https://bgm.tv/subject/${game.subjectId}`} target="_blank" rel="noreferrer">Bangumi 条目</a>}{game.vndbId && <a href={`https://vndb.org/${game.vndbId}`} target="_blank" rel="noreferrer">VNDB 条目</a>}{game.metadataSource && <Tag>资料来源：{game.metadataSource === 'vndb' ? 'VNDB' : 'Bangumi'}</Tag>}{!game.subjectId && !game.vndbId && <Tag>未绑定条目</Tag>}</Space><Typography.Paragraph style={{ whiteSpace: 'pre-wrap' }}>{game.summary || '暂无简介'}</Typography.Paragraph></div></div>
      {game.missing && <Alert type="warning" message="启动文件不可访问；恢复原位后会自动更新，也可在游戏资源中修改路径。" />}
      <Popconfirm title={`启动「${game.title}」？`} description={game.path} okText="启动游戏" cancelText="取消" disabled={busy || game.missing} onConfirm={() => launch(game.id)}><Button type="primary" disabled={busy || game.missing} aria-label="启动游戏">启动游戏</Button></Popconfirm>
      <Typography.Paragraph type="secondary" style={{ marginTop: 16 }}>标题、启动路径及 Bangumi／VNDB 绑定请在资源库 → 游戏资源中编辑。</Typography.Paragraph>
    </section> : <>
      <Input aria-label="搜索游戏" placeholder="搜索游戏名或开发商" value={query} allowClear onChange={(event) => setQuery(event.target.value)} />
      <List className="poster-library" grid={{ gutter: 20, xs: 2, sm: 3, md: 4, lg: 5, xl: 6, xxl: 6 }} dataSource={items.filter((game) => `${game.title} ${game.developer}`.toLocaleLowerCase().includes(query.toLocaleLowerCase()))} locale={{ emptyText: '暂无游戏，请在上方导入游戏可执行文件。' }} renderItem={(game) => <List.Item><Card hoverable cover={<GameCover game={game} />}><Button type="link" aria-label={`查看游戏 ${game.title}`} onClick={() => { setSelected(game.id); setMessage('') }}>{game.title}</Button><Typography.Text type="secondary">{game.missing ? '启动文件不可访问' : game.developer || '开发商待刮削'}</Typography.Text></Card></List.Item>} />
    </>}
  </Space>
}
export function GameResources() {
  const { items, setItems, loading, error, setError, load } = useGames()
  const [selected, setSelected] = useState<GameResource | null>(null)
  const [busy, setBusy] = useState(false)
  const [message, setMessage] = useState('')
  const [query, setQuery] = useState('')
  const [subjectId, setSubjectId] = useState('')
  const [candidates, setCandidates] = useState<(GameCandidate | VndbCandidate)[]>([])
  const [source, setSource] = useState('bangumi')
  const selectionGeneration = useRef(0)
  async function action(task: () => Promise<void>) {
    if (busy) return
    setBusy(true); setError(''); setMessage('')
    try { await task() } catch (error) { setError(errorText(error)) } finally { setBusy(false) }
  }
  async function scrape(subject?: number | string) {
    if (!selected) return
    const token = selectionGeneration.current
    const result = source === 'vndb' ? await scrapeGameVndb(selected.id, subject === undefined ? undefined : String(subject)) : await scrapeGame(selected.id, subject === undefined ? undefined : Number(subject))
    const updated = await getGames(); setItems(updated.items)
    if (token === selectionGeneration.current) { setSelected(updated.items.find((game) => game.id === selected.id) ?? null); setMessage(scrapeMessage(result)) }
  }
  return <Space direction="vertical" size="middle" style={{ width: '100%' }}>
    <Typography.Text type="secondary">整理游戏标题、简介、启动路径和来源绑定。Bangumi 优先，VNDB 为视觉小说备用来源；新游戏请在游戏库中导入。</Typography.Text>
    {error && <Alert type="error" message={error} />}{message && <Alert type="info" message={message} />}
    <Button disabled={busy} loading={loading} onClick={() => void load()}>刷新游戏资源</Button>
    {loading ? <Spin aria-label="正在加载游戏资源" /> : <Table rowKey="id" dataSource={items} pagination={{ pageSize: 20 }} columns={[
      { title: '游戏', dataIndex: 'title' }, { title: '开发商', dataIndex: 'developer' }, { title: '状态', render: (_, game: GameResource) => game.missing ? '不可访问' : '可启动' },
      { title: '来源', render: (_, game: GameResource) => <Space direction="vertical">{game.subjectId && <a href={`https://bgm.tv/subject/${game.subjectId}`} target="_blank" rel="noreferrer">Bangumi {game.subjectId}</a>}{game.vndbId && <a href={`https://vndb.org/${game.vndbId}`} target="_blank" rel="noreferrer">VNDB {game.vndbId}</a>}{!game.subjectId && !game.vndbId && '未绑定'}</Space> },
      { title: '操作', render: (_, game: GameResource) => <Space><Button disabled={busy} aria-label={`编辑游戏 ${game.title}`} onClick={() => { selectionGeneration.current++; setSelected({ ...game }); setSource(game.metadataSource === 'vndb' ? 'vndb' : 'bangumi'); setQuery(game.title); setSubjectId(''); setCandidates([]); setMessage('') }}>编辑</Button>{game.missing && <Popconfirm title={`删除「${game.title}」词条？`} description="删除游戏记录、绑定和封面，磁盘文件不会删除。" okText="确认删除" cancelText="取消" disabled={busy} onConfirm={() => action(async () => { await removeUnavailableGame(game.id); setItems((old) => old.filter((item) => item.id !== game.id)); if (selected?.id === game.id) { selectionGeneration.current++; setSelected(null) } setMessage('游戏词条已删除。') })}><Button danger disabled={busy} aria-label={`删除游戏 ${game.title}`}>删除词条</Button></Popconfirm>}</Space> },
    ]} />}
    {selected && <section className="settings-section"><Space direction="vertical" size="middle" style={{ width: '100%' }}>
      <Typography.Title level={4}>编辑游戏</Typography.Title>
      <label>游戏标题<Input aria-label="游戏标题" value={selected.title} maxLength={200} disabled={busy} onChange={(event) => setSelected({ ...selected, title: event.target.value })} /></label>
      <label>开发商<Input aria-label="游戏开发商" value={selected.developer} maxLength={200} disabled={busy} onChange={(event) => setSelected({ ...selected, developer: event.target.value })} /></label>
      <label>简介<Input.TextArea aria-label="游戏简介" value={selected.summary} maxLength={6000} rows={4} disabled={busy} onChange={(event) => setSelected({ ...selected, summary: event.target.value })} /></label>
      <label>启动文件路径<Input aria-label="游戏启动路径" value={selected.path} maxLength={4096} disabled={busy} onChange={(event) => setSelected({ ...selected, path: event.target.value })} /></label>
      <Button loading={busy} disabled={!selected.title.trim() || !selected.path.trim()} onClick={() => void action(async () => { const game = await editGame(selected.id, selected); setSelected(game); setItems((old) => old.map((item) => item.id === game.id ? game : item)); setMessage('游戏信息已保存；后续刮削保留人工编辑。') })}>保存游戏信息</Button>
      <Typography.Title level={5}>游戏刮削与绑定</Typography.Title>
      <label>刮削来源<select aria-label="游戏刮削来源" value={source} disabled={busy} onChange={(event) => { setSource(event.target.value); setCandidates([]); setSubjectId('') }}><option value="bangumi">Bangumi（首选，自动匹配时允许 VNDB 备用）</option><option value="vndb">VNDB（视觉小说备用）</option></select></label>
      <Button loading={busy} onClick={() => void action(() => scrape())}>{source === 'vndb' ? '匹配／刷新 VNDB' : '自动匹配／刷新游戏刮削'}</Button>
      <Space.Compact style={{ width: '100%' }}><Input aria-label="游戏条目搜索词" value={query} disabled={busy} maxLength={65} onChange={(event) => setQuery(event.target.value)} /><Button disabled={busy || !query.trim()} onClick={() => void action(async () => setCandidates(source === 'vndb' ? (await searchGameVndb(query.trim())).items : (await searchGameBangumi(query.trim())).items))}>搜索游戏条目</Button></Space.Compact>
      <List dataSource={candidates} locale={{ emptyText: '可搜索原名、中文名或别名。' }} renderItem={(candidate) => <List.Item actions={[<Button key="bind" disabled={busy} onClick={() => void action(() => scrape(candidate.id))}>绑定此条目</Button>]}><Space wrap><a href={source === 'vndb' ? `https://vndb.org/${candidate.id}` : `https://bgm.tv/subject/${candidate.id}`} target="_blank" rel="noreferrer">{candidate.nameCn || candidate.name}</a><Typography.Text type="secondary">{candidate.platform} · {candidate.id}</Typography.Text></Space></List.Item>} />
      <Space.Compact style={{ width: '100%' }}><Input aria-label="游戏条目 ID" value={subjectId} disabled={busy} placeholder={source === 'vndb' ? 'VNDB ID，例如 v4' : 'Bangumi 游戏条目 ID'} onChange={(event) => setSubjectId(event.target.value.trim().toLowerCase())} /><Button disabled={busy || !(source === 'vndb' ? /^v[1-9][0-9]{0,9}$/.test(subjectId) : /^[1-9][0-9]{0,9}$/.test(subjectId))} onClick={() => void action(() => scrape(subjectId))}>确认绑定</Button></Space.Compact>
    </Space></section>}
  </Space>
}
