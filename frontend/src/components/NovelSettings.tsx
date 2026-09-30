import { useEffect, useState } from 'react'
import { Alert, Button, Input, List, Space, Spin, Typography } from 'antd'
import { getNovelReader, getNovelSources, importNovels, saveNovelReader } from '../api/client'
import type { NovelReader, NovelSource } from '../api/types'

export function NovelSettings() {
  const [reader, setReader] = useState<NovelReader | null>(null)
  const [sources, setSources] = useState<NovelSource[]>([])
  const [path, setPath] = useState('')
  const [busy, setBusy] = useState(false)
  const [message, setMessage] = useState('')
  const [error, setError] = useState('')
  useEffect(() => {
    let active = true
    void Promise.all([getNovelReader(), getNovelSources()]).then(([r, s]) => { if (active) { setReader(r); setSources(s.items) } }).catch((e) => { if (active) setError(e instanceof Error ? e.message : '加载失败') })
    return () => { active = false }
  }, [])
  async function save() {
    if (!reader || busy) return
    setBusy(true); setError(''); setMessage('')
    try { setReader(await saveNovelReader(reader)); setMessage('阅读器已保存。') }
    catch (e) { setError(e instanceof Error ? e.message : '保存失败') }
    finally { setBusy(false) }
  }
  async function scan(value: string) {
    if (busy) return
    setBusy(true); setError(''); setMessage('')
    try { const result = await importNovels(value.trim()); setMessage(`已扫描 ${result.fileCount} 个小说文件，后台已开始 Bangumi 刮削；稍后刷新小说库。`); setSources((await getNovelSources()).items) }
    catch (e) { setError(e instanceof Error ? e.message : '导入失败') }
    finally { setBusy(false) }
  }
  return <Space direction="vertical" size="middle" style={{ width: '100%' }}>
    <Typography.Title level={4}>小说阅读与导入</Typography.Title>
    {error && <Alert type="error" message={error} />}{message && <Alert type="success" message={message} />}
    {!reader && !error && <Spin aria-label="正在加载阅读器配置" />}
    {reader && <>
      <label>默认阅读器<select aria-label="默认阅读器" value={reader.type} disabled={busy} onChange={(e) => setReader({ type: e.target.value as NovelReader['type'], executable: '' })}><option value="system">系统默认关联</option><option value="custom">自定义阅读器（EXE）</option></select></label>
      {reader.type === 'custom' && <label>阅读器可执行文件<Input aria-label="阅读器可执行文件" value={reader.executable} disabled={busy} maxLength={1024} onChange={(e) => setReader({ ...reader, executable: e.target.value })} placeholder="例如 D:\\Readers\\SumatraPDF.exe" /></label>}
      <Typography.Text type="secondary">支持系统已关联的阅读器，或可通过文件参数打开 EPUB／TXT／PDF 的本机阅读器。不同格式能否阅读取决于阅读器本身；不支持命令行参数模板。</Typography.Text>
      <Button loading={busy} onClick={() => void save()} disabled={reader.type === 'custom' && !reader.executable.trim()}>保存阅读器</Button>
    </>}
    <label>小说文件夹或文件路径<Input aria-label="小说文件夹或文件路径" value={path} disabled={busy} maxLength={4096} onChange={(e) => setPath(e.target.value)} placeholder="粘贴本机文件夹，或 EPUB／TXT／PDF 文件的绝对路径" /></label>
    <Typography.Text type="secondary">原位扫描，不复制、移动或删除文件。文件夹递归导入；重扫会更新丢失状态。Bangumi 配置与番剧共用。</Typography.Text>
    <Button type="primary" loading={busy} disabled={!path.trim()} onClick={() => void scan(path)}>导入小说资源</Button>
    <List size="small" dataSource={sources} locale={{ emptyText: '暂无小说来源' }} renderItem={(source) => <List.Item actions={[<Button key="rescan" disabled={busy} onClick={() => void scan(source.path)}>重新扫描</Button>]}><Typography.Text style={{ overflowWrap: 'anywhere' }}>{source.directory ? '文件夹' : '文件'} · {source.path}</Typography.Text></List.Item>} />
  </Space>
}
