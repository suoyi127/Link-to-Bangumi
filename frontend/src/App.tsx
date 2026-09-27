import { lazy, Suspense, useState } from 'react'
import { ConfigProvider, Layout, Menu, Spin, Typography } from 'antd'

const DashboardPage = lazy(() => import('./components/DashboardPage').then(({ DashboardPage }) => ({ default: DashboardPage })))
const InboxPage = lazy(() => import('./components/InboxPage').then(({ InboxPage }) => ({ default: InboxPage })))
const LibraryPage = lazy(() => import('./components/LibraryPage').then(({ LibraryPage }) => ({ default: LibraryPage })))
const AnimeDetailPage = lazy(() => import('./components/AnimeDetailPage').then(({ AnimeDetailPage }) => ({ default: AnimeDetailPage })))
const SettingsPage = lazy(() => import('./components/SettingsPage').then(({ SettingsPage }) => ({ default: SettingsPage })))

const { Header, Sider, Content } = Layout
const pages = [
  { key: 'dashboard', label: '仪表盘' },
  { key: 'inbox', label: '待整理' },
  { key: 'library', label: '番剧库' },
  { key: 'settings', label: '设置' },
]

export default function App() {
  const [page, setPage] = useState('dashboard')
  const [animeId, setAnimeId] = useState<number | null>(null)
  const title = pages.find((item) => item.key === page)?.label ?? '仪表盘'

  return (
    <ConfigProvider>
      <Layout style={{ minHeight: '100vh' }}>
        <Sider theme="light" breakpoint="lg" collapsedWidth="0" style={{ position: 'sticky', top: 0, height: '100vh', overflowY: 'auto', alignSelf: 'flex-start' }}>
          <Typography.Title level={4} style={{ margin: 24 }}>Anime Vault</Typography.Title>
          <Menu mode="inline" selectedKeys={[page]} items={pages} onClick={({ key }) => { setAnimeId(null); setPage(key) }} />
        </Sider>
        <Layout>
          <Header style={{ background: '#fff', paddingInline: 24 }}>
            <Typography.Title level={4} style={{ margin: 0, lineHeight: '64px' }}>{title}</Typography.Title>
          </Header>
          <Content style={{ padding: 24 }}>
            <Suspense fallback={<Spin aria-label="正在加载页面" />}>
              {page === 'dashboard' ? <DashboardPage /> : page === 'inbox' ? <InboxPage onOpenAnime={(id) => { setAnimeId(id); setPage('library') }} /> : page === 'library' ? animeId ? <AnimeDetailPage animeId={animeId} onBack={() => setAnimeId(null)} /> : <LibraryPage onOpenAnime={setAnimeId} /> : <SettingsPage />}
            </Suspense>
          </Content>
        </Layout>
      </Layout>
    </ConfigProvider>
  )
}
