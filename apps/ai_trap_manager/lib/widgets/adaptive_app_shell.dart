// Copyright 2026 Nature Sense
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../providers/trap_provider.dart';
import '../screens/trap_screen.dart';
import '../screens/sessions_screen.dart';
import '../screens/monitoring_screen.dart';
import '../screens/config_screen.dart';
import '../screens/sessions_master_detail.dart';
import '../screens/connect_trap_dialog.dart';

/// Adaptive app shell that switches between phone and tablet layouts
/// based on screen width.
///
/// - **Phone** (< 600dp): BottomNavigationBar + IndexedStack (current behavior)
/// - **Tablet** (≥ 600dp): NavigationRail on the left + content area.
///   The Sessions tab uses a master-detail split view.
class AdaptiveAppShell extends StatefulWidget {
  const AdaptiveAppShell({super.key});

  @override
  State<AdaptiveAppShell> createState() => _AdaptiveAppShellState();
}

class _AdaptiveAppShellState extends State<AdaptiveAppShell> {
  int _selectedIndex = 0;

  // ── Tab definitions shared by both layouts ──────────────────────────────
  static const _tabs = <_TabItem>[
    _TabItem(icon: Icons.videocam, selectedIcon: Icons.videocam, label: 'Trap'),
    _TabItem(icon: Icons.history, selectedIcon: Icons.history, label: 'Sessions'),
    _TabItem(icon: Icons.monitor_heart_outlined, selectedIcon: Icons.monitor_heart, label: 'Monitoring'),
    _TabItem(icon: Icons.settings, selectedIcon: Icons.settings, label: 'Config'),
  ];

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      _showConnectDialog();
      _listenForConnectionLost();
    });
  }

  /// Listen for unexpected connection loss and auto-show the reconnect dialog.
  void _listenForConnectionLost() {
    final trapProvider = context.read<TrapProvider>();
    trapProvider.addListener(_onProviderChange);
  }

  void _onProviderChange() {
    final trapProvider = context.read<TrapProvider>();
    if (trapProvider.connectionLost && mounted) {
      // Remove listener to prevent re-entry while dialog is showing
      trapProvider.removeListener(_onProviderChange);

      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Lost connection to trap'),
          duration: Duration(seconds: 2),
        ),
      );

      // Brief delay so the user sees the SnackBar, then show reconnect dialog
      Future.delayed(const Duration(milliseconds: 500), () {
        if (mounted) {
          _showConnectDialog().then((_) {
            // Re-attach listener after dialog closes
            if (mounted) {
              context.read<TrapProvider>().addListener(_onProviderChange);
            }
          });
        }
      });
    }
  }

  Future<void> _showConnectDialog() async {
    final connected = await ConnectTrapDialog.show(context);
    if (!connected && mounted) {
      // User exited without connecting — show the empty shell
    }
  }

  Future<void> _disconnectAndReconnect() async {
    context.read<TrapProvider>().disconnectFromTrap();
    if (mounted) {
      await _showConnectDialog();
    }
  }

  @override
  Widget build(BuildContext context) {
    final trapProvider = Provider.of<TrapProvider>(context);
    final isWide = MediaQuery.of(context).size.width >= 600;

    return Scaffold(
      appBar: AppBar(
        title: const Text('AI Insect Trap Manager'),
        backgroundColor: Theme.of(context).colorScheme.primaryContainer,
        actions: [
          // Connection indicator
          Padding(
            padding: const EdgeInsets.only(right: 12),
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Container(
                  width: 8,
                  height: 8,
                  decoration: BoxDecoration(
                    color: trapProvider.isConnected ? Colors.green : Colors.red,
                    shape: BoxShape.circle,
                  ),
                ),
                const SizedBox(width: 4),
                Text(
                  trapProvider.isConnected ? trapProvider.trapIp : 'Disconnected',
                  style: TextStyle(
                    fontSize: 12,
                    color: trapProvider.isConnected ? Colors.green : Colors.red,
                  ),
                ),
              ],
            ),
          ),
          // Disconnect / Reconnect button
          if (trapProvider.isConnected)
            IconButton(
              onPressed: _disconnectAndReconnect,
              icon: const Icon(Icons.link_off),
              tooltip: 'Disconnect',
              color: Colors.red.shade300,
            )
          else
            IconButton(
              onPressed: _showConnectDialog,
              icon: const Icon(Icons.link),
              tooltip: 'Connect to trap',
            ),
        ],
      ),
      body: isWide ? _buildTabletLayout() : _buildPhoneLayout(),
      bottomNavigationBar:
          isWide ? null : _buildBottomNav(),
    );
  }

  // ── Phone layout: BottomNavigationBar + IndexedStack ───────────────────
  Widget _buildPhoneLayout() {
    return IndexedStack(
      index: _selectedIndex,
      children: const [
        TrapScreen(),
        SessionsScreen(),
        MonitoringScreen(),
        ConfigScreen(),
      ],
    );
  }

  Widget _buildBottomNav() {
    return BottomNavigationBar(
      items: List.generate(_tabs.length, (i) {
        final tab = _tabs[i];
        return BottomNavigationBarItem(
          icon: Icon(tab.icon),
          activeIcon: Icon(tab.selectedIcon),
          label: tab.label,
        );
      }),
      currentIndex: _selectedIndex,
      selectedItemColor: Colors.green[700],
      unselectedItemColor: Colors.grey,
      onTap: (index) => setState(() => _selectedIndex = index),
      type: BottomNavigationBarType.fixed,
    );
  }

  // ── Tablet layout: NavigationRail + content ────────────────────────────
  Widget _buildTabletLayout() {
    return Row(
      children: [
        NavigationRail(
          selectedIndex: _selectedIndex,
          onDestinationSelected: (index) =>
              setState(() => _selectedIndex = index),
          labelType: NavigationRailLabelType.all,
          leading: Padding(
            padding: const EdgeInsets.symmetric(vertical: 8),
            child: Icon(
              Icons.bug_report,
              size: 32,
              color: Theme.of(context).colorScheme.primary,
            ),
          ),
          destinations: List.generate(_tabs.length, (i) {
            final tab = _tabs[i];
            return NavigationRailDestination(
              icon: Icon(tab.icon),
              selectedIcon: Icon(tab.selectedIcon),
              label: Text(tab.label),
            );
          }),
        ),
        const VerticalDivider(width: 1),
        // Content area
        Expanded(child: _buildTabletContent()),
      ],
    );
  }

  Widget _buildTabletContent() {
    // Sessions tab uses master-detail on tablet
    if (_selectedIndex == 1) {
      return const SessionsMasterDetail();
    }

    // Other tabs use the same screens
    switch (_selectedIndex) {
      case 0:
        return const TrapScreen();
      case 2:
        return const MonitoringScreen();
      case 3:
        return const ConfigScreen();
      default:
        return const SizedBox.shrink();
    }
  }
}

/// Internal model for tab metadata.
class _TabItem {
  final IconData icon;
  final IconData selectedIcon;
  final String label;

  const _TabItem({
    required this.icon,
    required this.selectedIcon,
    required this.label,
  });
}
