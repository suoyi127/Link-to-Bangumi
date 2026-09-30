import { useEffect, useRef, useState } from 'react'
import { Alert, Button, Card, Input, List, Space, Spin, Tag, Typography } from 'antd'
import { getNovels, readNovel } from '../api/client'
import type { NovelWork } from '../api/types'

export function NovelLibraryPage() {
  const [items, setItems] = useState<NovelWork[]>([])
  const [selected, setSelected] = useState<number | null>(null)
  const [query, setQuery] = useState('')
  const [loading, setLoading] = useState(true)
  const [busy, setBusy] = useState(false)
  const [message, setMessage] = useState('')
  const [error, setError] = useState('')
  const loadSequence = useRef(0)
  async function load(signal?: AbortSignal, quiet = false) {
    const sequence = ++loadSequence.current
    if (!quiet) setLoading(true)
    try {
      const result = await getNovels(signal)
      if (!signal?.aborted && sequence === loadSequence.current) {
        // 删除后清空失效详情，只允许最新响应更新列表，防止旧请求复活卷信息。
        setItems(result.items)
        setSelected((id) => id !== null && result.items.some((item) => item.id === id) ? id : null)
        if (!quiet) setError('')
      }
    }
    catch (e) { if (!signal?.aborted && sequence === loadSequence.current && !quiet) setError(e instanceof Error ? e.message : '加载失败') }
    finally { if (!signal?.aborted && sequence === loadSequence.current) setLoading(false) }
  }
  useEffect(() => {
    const requests = loadSequence
    let controller = new AbortController()
    void load(controller.signal)
    // 后台更新保留详情和阅读提示；页面离开时取消请求，避免旧结果回写。
    const refresh = () => {
      if (document.visibilityState === 'hidden') return
      controller.abort(); controller = new AbortController(); void load(controller.signal, true)
    }
    const timer = window.setInterval(refresh, 5000)
    window.addEventListener('focus', refresh)
    document.addEventListener('visibilitychange', refresh)
    return () => { requests.current++; controller.abort(); window.clearInterval(timer); window.removeEventListener('focus', refresh); document.removeEventListener('visibilitychange', refresh) }
  }, [])
  const work = items.find((item) => item.id === selected)
  async function read(id: number) {
    setBusy(true); setMessage(''); setError('')
    try { await readNovel(id); setMessage('已交给本机阅读器打开。') }
    catch (e) { setError(e instanceof Error ? e.message : '打开失败') }
    finally { setBusy(false) }
  }
  return <Space direction="vertical" size="large" style={{ width: '100%' }}>
    {error && <Alert type="error" message={error} />}{message && <Alert type="success" message={message} />}
    <Space wrap>{work && <Button onClick={() => setSelected(null)}>返回小说库</Button>}<Button loading={loading} onClick={() => void load()}>刷新小说库</Button></Space>
    {loading ? <Spin aria-label="正在加载小说库" /> : work ? <section className="novel-detail settings-section">
      <div className="novel-detail-header"><NovelCover work={work} /><div><Typography.Title level={3}>{work.title}</Typography.Title><Typography.Paragraph>{work.author || '作者待刮削'}</Typography.Paragraph>{work.subjectId ? <a href={`https://bgm.tv/subject/${work.subjectId}`} target="_blank" rel="noreferrer">Bangumi 条目</a> : <Tag>未绑定 Bangumi</Tag>}<Typography.Paragraph style={{ whiteSpace: 'pre-wrap' }}>{work.summary || '暂无简介'}</Typography.Paragraph></div></div>
      <Typography.Title level={4}>卷与文件</Typography.Title>
      <List dataSource={work.files} renderItem={(file) => <List.Item actions={[<Button key="read" aria-label="阅读" disabled={busy || file.missing} onClick={() => void read(file.id)}>阅读</Button>]}><Space wrap><span>{file.label}</span>{file.missing && <Tag color="warning">文件不存在或不可访问，恢复后自动更新</Tag>}{file.subjectId && <a href={`https://bgm.tv/subject/${file.subjectId}`} target="_blank" rel="noreferrer">本卷条目</a>}</Space></List.Item>} />
      <Typography.Text type="secondary">元数据及卷归属请在资源库 → 小说资源中编辑。</Typography.Text>
    </section> : <>
      <Input aria-label="搜索小说" placeholder="搜索书名或作者" value={query} onChange={(e) => setQuery(e.target.value)} allowClear />
      <List className="poster-library" grid={{ gutter: 20, xs: 2, sm: 3, md: 4, lg: 5, xl: 6, xxl: 6 }} dataSource={items.filter((w) => `${w.title} ${w.author}`.toLocaleLowerCase().includes(query.toLocaleLowerCase()))} locale={{ emptyText: '暂无小说，请在设置中导入小说文件夹或文件。' }} renderItem={(w) => <List.Item><Card hoverable cover={<NovelCover work={w} />}><Button type="link" aria-label={`查看 ${w.title}`} onClick={() => { setSelected(w.id); setMessage('') }}>{w.title}</Button><Typography.Text type="secondary">{w.files.filter((f) => !f.missing).length} 个可读文件 · {w.author || '作者待刮削'}</Typography.Text></Card></List.Item>} />
    </>}
  </Space>
}
function NovelCover({ work }: { work: NovelWork }) {
  const [failedUrl, setFailedUrl] = useState('')
  return <div className="anime-cover" style={{ position: 'relative', width: '100%', aspectRatio: '2 / 3', overflow: 'hidden', background: '#edf2f4', display: 'grid', placeItems: 'center', color: '#83959e' }}>{work.coverUrl && failedUrl !== work.coverUrl ? <img src={work.coverUrl} alt={`${work.title} 封面`} style={{ position: 'absolute', inset: 0, width: '100%', height: '100%', objectFit: 'cover' }} onError={() => setFailedUrl(work.coverUrl)} /> : <span>暂无封面</span>}</div>
}
