import React from 'react';
import { Link, useLocation } from 'react-router-dom';
import {
  LayoutDashboard,
  Play,
  Search,
  Radio,
  Settings,
  Server,
} from 'lucide-react';
import { cn } from '@/lib/utils';
import { Button } from '@/components/ui/button';
import { Separator } from '@/components/ui/separator';
import { Toaster } from 'sonner';
import { SessionStatusBar } from '@/components/common/SessionStatusBar';

interface NavItem {
  path: string;
  label: string;
  icon: React.ReactNode;
}

const navItems: NavItem[] = [
  { path: '/', label: 'Dashboard', icon: <LayoutDashboard className="h-5 w-5" /> },
  { path: '/sessions', label: 'Sessions', icon: <Play className="h-5 w-5" /> },
  { path: '/explorer', label: 'API Explorer', icon: <Search className="h-5 w-5" /> },
  { path: '/websocket', label: 'WebSocket', icon: <Radio className="h-5 w-5" /> },
  { path: '/settings', label: 'Settings', icon: <Settings className="h-5 w-5" /> },
];

interface LayoutProps {
  children: React.ReactNode;
}

export function Layout({ children }: LayoutProps) {
  const location = useLocation();

  return (
    <div className="flex h-screen bg-background">
      {/* Sidebar */}
      <aside className="w-64 border-r bg-card flex flex-col">
        {/* Logo */}
        <div className="p-4 flex items-center gap-3">
          <div className="h-10 w-10 rounded-lg bg-primary/10 flex items-center justify-center">
            <Server className="h-6 w-6 text-primary" />
          </div>
          <div>
            <h1 className="font-semibold text-lg">BrokerSim</h1>
            <p className="text-xs text-muted-foreground">Manager</p>
          </div>
        </div>

        <Separator />

        {/* Navigation */}
        <nav className="flex-1 p-4 space-y-1">
          {navItems.map((item) => (
            <Link key={item.path} to={item.path}>
              <Button
                variant={location.pathname === item.path ? 'secondary' : 'ghost'}
                className={cn(
                  'w-full justify-start gap-3',
                  location.pathname === item.path && 'bg-secondary'
                )}
              >
                {item.icon}
                {item.label}
              </Button>
            </Link>
          ))}
        </nav>

        {/* Footer */}
        <div className="p-4 border-t">
          <div className="text-xs text-muted-foreground">
            <p>Connected to: <span className="text-foreground">elanlinux</span></p>
            <p className="mt-1">Version 1.0.0</p>
          </div>
        </div>
      </aside>

      {/* Main Content */}
      <main className="flex-1 flex flex-col min-h-0">
        <div className="flex-1 overflow-auto">
          {children}
        </div>
        <SessionStatusBar />
      </main>

      {/* Toast notifications */}
      <Toaster position="bottom-right" richColors />
    </div>
  );
}
