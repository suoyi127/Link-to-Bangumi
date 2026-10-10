import { useEffect, useRef, useState } from 'react'
import { Alert, Button, Card, Input, List, Space, Spin, Table, Tabs, Tag, Typography } from 'antd'
import { getAnime } from '../api/client'
import type { Anime, AnimeSort } from '../api/types'
import { AnimeCover } from './AnimeCover'
import { LibraryCalendar } from './LibraryCalendar'

type Props = { onOpenAnime: (id: number) => void }
const pageSize = 200
const errorText = (error: unknown) => error instanceof Error ? error.message : '加载失败'

export function LibraryPage({ onOpenAnime }: Props) {
  // 番剧目录采用分页加载；文件列表、别名与操作入口放在详情页按需读取。
  const [view, setView] = useState<'grid' | 'list'>('grid')
  const [mode, setMode] = useState<'all' | 'calendar'>('all')
  const [filter, setFilter] = useState('')
  const [sort, setSort] = useState<AnimeSort>('import_desc')
  const [items, setItems] = useState<Anime[]>([])
  const [offset, setOffset] = useState(0)
  const [nextOffset, setNextOffset] = useState<number | null>(null)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const sequence = useRef(0)
  useEffect(() => {
    if (mode !== 'all') return
    const controller = new AbortController()
    const sequenceRef = sequence
    const current = ++sequenceRef.current
    setLoading(true)
    setError('')
    let pending = false
    const load = () => {
      if (pending) return
      pending = true
      const request = sort === 'import_desc' ? getAnime(pageSize, offset, controller.signal) : getAnime(pageSize, offset, controller.signal, sort)
      void request.then((page) => {
        if (current !== sequence.current) return
        setItems(page.items)
        setNextOffset(page.nextOffset)
      }).catch((cause) => { if (!controller.signal.aborted && current === sequence.current) setError(errorText(cause)) })
        .finally(() => { pending = false; if (!controller.signal.aborted && current === sequence.current) setLoading(false) })
    }
    load()
    const timer = setInterval(load, 10_000)
    return () => { clearInterval(timer); controller.abort(); if (sequenceRef.current === current) sequenceRef.current++ }
  }, [offset, mode, sort])

  const title = (item: Anime) => <Button type="link" aria-label={`打开 ${item.displayTitle}`} onClick={() => onOpenAnime(item.id)}>{item.displayTitle}</Button>
  const coverCell = (item: Anime) => <AnimeCover key={`${item.id}-${item.coverUrl}`} item={item} />
  const visibleItems = items.filter((item) => `${item.displayTitle} ${item.originalTitle}`.toLocaleLowerCase().includes(filter.trim().toLocaleLowerCase()))
  const groups = new Map<string, Anime[]>()
  for (const item of visibleItems) {
    const month = item.importedMonth || ''
    if (!groups.has(month)) groups.set(month, [])
    groups.get(month)!.push(item)
  }
  const monthTitle = (month: string) => month ? `${month.slice(0, 4)} 年 ${month.slice(5, 7)} 月` : '导入月份未知'
  return <Space className="poster-library" direction="vertical" size="large" style={{ width: '100%' }}>
    <Tabs activeKey={mode} onChange={(value) => setMode(value as 'all' | 'calendar')} items={[{ key: 'all', label: '全部番剧' }, { key: 'calendar', label: '每周排期' }]} />
    {mode === 'calendar' ? <><Typography.Title level={3} style={{ margin: 0 }}>每周排期</Typography.Title><LibraryCalendar onOpenAnime={onOpenAnime} /></> : <>
    <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', flexWrap: 'wrap', gap: 12 }}>
      <div><Typography.Title level={3} style={{ margin: 0 }}>媒体库</Typography.Title><Typography.Text type="secondary">按番剧浏览 · 当前页 {items.length} 部</Typography.Text></div>
      <Space wrap><Input.Search aria-label="筛选本页番剧" placeholder="筛选本页番剧" value={filter} onChange={(event) => setFilter(event.target.value)} style={{ width: 240 }} allowClear /><Button aria-label="网格视图" type={view === 'grid' ? 'primary' : 'default'} onClick={() => setView('grid')}>网格</Button><Button aria-label="列表视图" type={view === 'list' ? 'primary' : 'default'} onClick={() => setView('list')}>列表</Button></Space>
    </div>
    {error && <Alert type="error" showIcon message={error} />}
    <label>导入月份排序 <select aria-label="导入月份排序" value={sort} onChange={(event) => { setSort(event.target.value as AnimeSort); setOffset(0) }}><option value="import_desc">最新导入在前</option><option value="import_asc">最早导入在前</option></select></label>
    {loading ? <Spin aria-label="正在加载番剧库" /> : items.length === 0 ? <Typography.Text type="secondary">暂无本地番剧</Typography.Text> : visibleItems.length === 0 ? <Typography.Text type="secondary">本页没有匹配的番剧</Typography.Text> : [...groups].map(([month, monthItems]) => <section key={month} aria-label={monthTitle(month)}><Typography.Title level={4}>{monthTitle(month)}</Typography.Title>{view === 'grid' ?
      <List grid={{ gutter: 20, xs: 2, sm: 3, md: 4, lg: 5, xl: 6, xxl: 6 }} dataSource={monthItems} renderItem={(item) => <List.Item><Card hoverable cover={coverCell(item)} styles={{ body: { padding: 12 } }} style={{ overflow: 'hidden' }}><div style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{title(item)}</div><Typography.Text type="secondary">{item.importedMonth && `${item.importedMonth} 导入`} {item.season || '季数未标注'}</Typography.Text><div><Tag color={item.bangumiSubjectId ? 'green' : 'default'}>{item.bangumiSubjectId ? '已绑定 Bangumi' : '未绑定 Bangumi'}</Tag></div></Card></List.Item>} /> :
      <Table rowKey="id" pagination={false} dataSource={monthItems} columns={[{ title: '封面', width: 90, render: (_, item) => <div style={{ width: 60 }}>{coverCell(item)}</div> }, { title: '标题', render: (_, item) => title(item) }, { title: '季', dataIndex: 'season' }, { title: '导入月份', dataIndex: 'importedMonth' }, { title: 'Bangumi', render: (_, item) => item.bangumiSubjectId ? '已绑定' : '未绑定' }]} />}</section>)}
    <Space><Typography.Text>第 {Math.floor(offset / pageSize) + 1} 页</Typography.Text><Button disabled={loading || offset === 0} onClick={() => setOffset(Math.max(0, offset - pageSize))}>上一页</Button><Button disabled={loading || nextOffset === null} onClick={() => { if (nextOffset !== null) setOffset(nextOffset) }}>下一页</Button></Space>
    </>}
  </Space>
}
