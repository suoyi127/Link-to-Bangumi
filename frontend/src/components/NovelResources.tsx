import { useEffect, useRef, useState } from 'react'
import { Alert, Button, Input, List, Popconfirm, Space, Spin, Table, Typography } from 'antd'
import { editNovel, editNovelFile, getNovels, removeUnavailableNovel, scrapeNovel, searchNovelBangumi } from '../api/client'
import type { NovelCandidate, NovelFile, NovelWork } from '../api/types'

export function NovelResources() {
  const [items, setItems] = useState<NovelWork[]>([])
  const [selected, setSelected] = useState<NovelWork | null>(null)
  const [busy, setBusy] = useState(false)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const [message, setMessage] = useState('')
  const [target, setTarget] = useState('work')
  const [query, setQuery] = useState('')
  const [subjectId, setSubjectId] = useState('')
  const [candidates, setCandidates] = useState<NovelCandidate[]>([])
  const loadSequence = useRef(0)
  async function load(signal?: AbortSignal) {
    const sequence = ++loadSequence.current
    setLoading(true)
    try { const result = await getNovels(signal); if (!signal?.aborted && sequence === loadSequence.current) { setItems(result.items); setSelected((previous) => previous && result.items.some((item) => item.id === previous.id) ? previous : null) } }
    catch (e) { if (!signal?.aborted && sequence === loadSequence.current) setError(e instanceof Error ? e.message : '加载失败') }
    finally { if (!signal?.aborted && sequence === loadSequence.current) setLoading(false) }
  }
  useEffect(() => { const requests = loadSequence; const controller = new AbortController(); void load(controller.signal); return () => { requests.current++; controller.abort() } }, [])
  async function action(task: () => Promise<void>) {
    if (busy) return
    setBusy(true); setError(''); setMessage('')
    try { await task() } catch (e) { setError(e instanceof Error ? e.message : '请求失败') } finally { setBusy(false) }
  }
  async function scrape(id?: number) {
    if (!selected) return
    const result = await scrapeNovel(target === 'work' ? selected.id : Number(target), target !== 'work', id)
    setMessage(result.bound ? result.coverUpdated ? '绑定与封面刮削已完成。' : `已绑定，封面未更新（${result.errorCode}）。` : `未自动绑定（${result.errorCode}）；请搜索候选或输入 Bangumi 条目 ID。`)
    const updated = await getNovels(); setItems(updated.items); setSelected(updated.items.find((w) => w.id === selected.id) ?? null)
  }
  return <Space className="resource-page" direction="vertical" size="middle" style={{ width: '100%' }}>
    <Typography.Text type="secondary">编辑作品元数据、卷标题及归属；所有修改仅写入数据库，不改动源文件。</Typography.Text>
    {error && <Alert type="error" message={error} />}{message && <Alert type="info" message={message} />}
    <Button disabled={busy} loading={loading} onClick={() => void load()}>刷新小说资源</Button>
    {loading ? <Spin aria-label="正在加载小说资源" /> : <Table rowKey="id" dataSource={items} pagination={{ pageSize: 20 }} columns={[
      { title: '作品', dataIndex: 'title' }, { title: '作者', dataIndex: 'author' }, { title: '文件', render: (_, w: NovelWork) => `${w.files.filter((f) => !f.missing).length} / ${w.files.length}` },
      { title: 'Bangumi', render: (_, w: NovelWork) => w.subjectId ? <a href={`https://bgm.tv/subject/${w.subjectId}`} target="_blank" rel="noreferrer">{w.subjectId}</a> : '未绑定' },
      { title: '操作', render: (_, w: NovelWork) => <Button disabled={busy} aria-label={`编辑 ${w.title}`} onClick={() => { setSelected({ ...w }); setTarget('work'); setQuery(w.title); setSubjectId(''); setCandidates([]); setMessage('') }}>编辑</Button> },
      { title: '清理', render: (_, w: NovelWork) => w.files.every((f) => f.missing) ? <Popconfirm title={`删除「${w.title}」词条？`} description="删除作品、卷记录及绑定信息，不会删除磁盘文件。重新扫描存在的文件可再次导入。" okText="确认删除" cancelText="取消" disabled={busy} onConfirm={() => action(async () => {
        await removeUnavailableNovel(w.id)
        // 删除成功后作废之前的刷新，避免其旧快照重新写回已清理的记录。
        loadSequence.current++; setLoading(false)
        setItems((old) => old.filter((v) => v.id !== w.id))
        if (selected?.id === w.id) { setSelected(null); setTarget('work'); setCandidates([]) }
        setMessage('小说词条已删除，磁盘文件未改动。')
      })}><Button danger disabled={busy} aria-label={`删除词条 ${w.title}`}>删除词条</Button></Popconfirm> : null },
    ]} />}
    {selected && <section className="settings-section"><Space direction="vertical" size="middle" style={{ width: '100%' }}>
      <Typography.Title level={4}>编辑小说</Typography.Title>
      <label>作品标题<Input aria-label="小说作品标题" value={selected.title} disabled={busy} maxLength={200} onChange={(e) => setSelected({ ...selected, title: e.target.value })} /></label>
      <label>作者<Input aria-label="小说作者" value={selected.author} disabled={busy} maxLength={200} onChange={(e) => setSelected({ ...selected, author: e.target.value })} /></label>
      <label>简介<Input.TextArea aria-label="小说简介" value={selected.summary} disabled={busy} maxLength={6000} rows={4} onChange={(e) => setSelected({ ...selected, summary: e.target.value })} /></label>
      <Button loading={busy} disabled={!selected.title.trim()} onClick={() => void action(async () => { const w = await editNovel(selected.id, { title: selected.title, author: selected.author, summary: selected.summary }); setSelected(w); setItems((old) => old.map((v) => v.id === w.id ? w : v)); setMessage('小说元数据已保存，后续刮削不会覆盖人工编辑。') })}>保存小说元数据</Button>
      <Typography.Title level={5}>卷文件编辑</Typography.Title>
      {selected.files.map((file) => <NovelFileEditor key={`${file.id}-${file.workId}`} file={file} works={items} disabled={busy} onSave={(workId, label) => void action(async () => { await editNovelFile(file.id, workId, label); const result = await getNovels(); setItems(result.items); setSelected(result.items.find((w) => w.id === selected.id) ?? null); setMessage('卷信息已保存。') })} />)}
      <Typography.Title level={5}>Bangumi 刮削与绑定</Typography.Title>
      <label>绑定对象<select aria-label="小说绑定对象" value={target} disabled={busy} onChange={(e) => { setTarget(e.target.value); setCandidates([]); setSubjectId(''); setQuery(e.target.value === 'work' ? selected.title : selected.files.find((f) => f.id === Number(e.target.value))?.label ?? '') }}><option value="work">整部作品（系列）</option>{selected.files.map((f) => <option key={f.id} value={f.id}>{f.label}</option>)}</select></label>
      <Button loading={busy} onClick={() => void action(() => scrape())}>自动匹配／刷新刮削</Button>
      <Space.Compact style={{ width: '100%' }}><Input aria-label="小说 Bangumi 搜索词" value={query} disabled={busy} maxLength={65} onChange={(e) => setQuery(e.target.value)} /><Button disabled={busy || !query.trim()} onClick={() => void action(async () => setCandidates((await searchNovelBangumi(query.trim())).items))}>搜索书籍</Button></Space.Compact>
      <List size="small" dataSource={candidates} locale={{ emptyText: '搜索候选会显示在此处；未知分类请自行核对。' }} renderItem={(s) => <List.Item actions={[<Button key="bind" disabled={busy} onClick={() => void action(() => scrape(s.id))}>绑定此条目</Button>]}><Space wrap><a href={`https://bgm.tv/subject/${s.id}`} target="_blank" rel="noreferrer">{s.nameCn || s.name}</a><Typography.Text type="secondary">{s.platform || '分类未知'} · {s.series ? '系列' : '单卷／非系列'} · {s.id}</Typography.Text></Space></List.Item>} />
      <Space.Compact style={{ width: '100%' }}><Input aria-label="小说 Bangumi 条目 ID" value={subjectId} disabled={busy} onChange={(e) => setSubjectId(e.target.value)} placeholder="也可粘贴正整数条目 ID" /><Button disabled={busy || !/^[1-9][0-9]{0,9}$/.test(subjectId)} onClick={() => void action(() => scrape(Number(subjectId)))}>确认绑定 ID</Button></Space.Compact>
    </Space></section>}
  </Space>
}
function NovelFileEditor({ file, works, disabled, onSave }: { file: NovelFile; works: NovelWork[]; disabled: boolean; onSave: (workId: number, label: string) => void }) {
  const [label, setLabel] = useState(file.label)
  const [workId, setWorkId] = useState(file.workId)
  return <div className="novel-file-editor"><Typography.Text type="secondary" style={{ overflowWrap: 'anywhere' }}>{file.path}{file.missing ? ' · 文件已不存在' : ''}</Typography.Text><label>卷标题<Input aria-label={`卷标题 ${file.id}`} value={label} disabled={disabled} maxLength={200} onChange={(e) => setLabel(e.target.value)} /></label><label>所属作品<select aria-label={`卷归属 ${file.id}`} value={workId} disabled={disabled} onChange={(e) => setWorkId(Number(e.target.value))}>{works.map((w) => <option key={w.id} value={w.id}>{w.title}</option>)}</select></label><Button disabled={disabled || !label.trim()} onClick={() => onSave(workId, label)}>保存卷信息</Button></div>
}
