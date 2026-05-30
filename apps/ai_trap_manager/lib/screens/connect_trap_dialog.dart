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

/// Full-screen modal dialog shown on app startup to connect to a trap.
///
/// Shows a list of previously connected traps (most recent first) and a
/// text field to enter a new hostname. Tapping a history entry or the
/// Connect button attempts to connect. On success the dialog dismisses,
/// revealing the main app shell.
class ConnectTrapDialog extends StatefulWidget {
  const ConnectTrapDialog({super.key});

  /// Show the dialog as a full-screen modal and return true if connected.
  static Future<bool> show(BuildContext context) async {
    final result = await showDialog<bool>(
      context: context,
      barrierDismissible: false,
      builder: (_) => const ConnectTrapDialog(),
    );
    return result ?? false;
  }

  @override
  State<ConnectTrapDialog> createState() => _ConnectTrapDialogState();
}

class _ConnectTrapDialogState extends State<ConnectTrapDialog> {
  final _hostController = TextEditingController();
  bool _connecting = false;
  String? _error;
  bool _showManualEntry = false;

  @override
  void initState() {
    super.initState();
    // Load connection history after first frame
    WidgetsBinding.instance.addPostFrameCallback((_) {
      context.read<TrapProvider>().loadConnectionHistory();
    });
  }

  @override
  void dispose() {
    _hostController.dispose();
    super.dispose();
  }

  Future<void> _connect([String? hostname]) async {
    final host = (hostname ?? _hostController.text).trim();
    if (host.isEmpty) {
      setState(() => _error = 'Please enter a hostname or IP address');
      return;
    }

    setState(() {
      _connecting = true;
      _error = null;
    });

    final trapProvider = context.read<TrapProvider>();
    await trapProvider.connectToTrap(host);

    if (!mounted) return;

    if (trapProvider.isConnected) {
      Navigator.of(context).pop(true);
    } else {
      setState(() {
        _connecting = false;
        _error = trapProvider.connectionError ?? 'Connection failed';
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final trapProvider = Provider.of<TrapProvider>(context);
    final history = trapProvider.connectionHistory;

    return Scaffold(
      backgroundColor: Theme.of(context).colorScheme.surface,
      body: Center(
        child: SingleChildScrollView(
          padding: const EdgeInsets.symmetric(horizontal: 32),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              // ── App Icon ────────────────────────────────────────────────
              Icon(
                Icons.radar,
                size: 80,
                color: Theme.of(context).colorScheme.primary,
              ),
              const SizedBox(height: 16),
              Text(
                'AI Insect Trap Manager',
                style: Theme.of(context).textTheme.headlineMedium?.copyWith(
                      fontWeight: FontWeight.bold,
                    ),
              ),
              const SizedBox(height: 8),
              Text(
                'Connect to your camera trap to get started',
                style: Theme.of(context).textTheme.bodyLarge?.copyWith(
                      color: Colors.grey,
                    ),
              ),
              const SizedBox(height: 32),

              // ── Connection History ──────────────────────────────────────
              if (history.isNotEmpty && !_showManualEntry) ...[
                Row(
                  children: [
                    Text(
                      'Recent traps',
                      style: Theme.of(context).textTheme.titleSmall?.copyWith(
                            color: Colors.grey,
                          ),
                    ),
                    const Spacer(),
                    TextButton.icon(
                      onPressed: () => setState(() => _showManualEntry = true),
                      icon: const Icon(Icons.add, size: 18),
                      label: const Text('New'),
                    ),
                  ],
                ),
                const SizedBox(height: 8),
                ...history.map((host) => _HistoryTile(
                      hostname: host,
                      onTap: _connecting ? null : () => _connect(host),
                      onDelete: () =>
                          trapProvider.removeFromConnectionHistory(host),
                    )),
                const SizedBox(height: 16),
                // Clear all
                if (history.length > 1)
                  Center(
                    child: TextButton(
                      onPressed: () => trapProvider.clearConnectionHistory(),
                      child: const Text('Clear history'),
                    ),
                  ),
              ],

              // ── Manual Entry ────────────────────────────────────────────
              if (_showManualEntry || history.isEmpty) ...[
                if (history.isNotEmpty)
                  Row(
                    children: [
                      Text(
                        'Enter trap address',
                        style: Theme.of(context)
                            .textTheme
                            .titleSmall
                            ?.copyWith(color: Colors.grey),
                      ),
                      const Spacer(),
                      TextButton(
                        onPressed: () =>
                            setState(() => _showManualEntry = false),
                        child: const Text('Back'),
                      ),
                    ],
                  ),
                const SizedBox(height: 8),
                TextField(
                  controller: _hostController,
                  decoration: InputDecoration(
                    labelText: 'Trap Hostname',
                    hintText: 'e.g. radxa-zero3.local',
                    border: const OutlineInputBorder(),
                    prefixIcon: const Icon(Icons.computer),
                    suffixIcon: _hostController.text.isNotEmpty
                        ? IconButton(
                            icon: const Icon(Icons.clear),
                            onPressed: () {
                              _hostController.clear();
                              setState(() => _error = null);
                            },
                          )
                        : null,
                  ),
                  keyboardType: TextInputType.text,
                  textInputAction: TextInputAction.go,
                  onSubmitted: (_) => _connect(),
                  enabled: !_connecting,
                ),
                const SizedBox(height: 16),
              ],

              // ── Error Message ───────────────────────────────────────────
              if (_error != null)
                Container(
                  padding: const EdgeInsets.all(12),
                  decoration: BoxDecoration(
                    color: Colors.red.shade50,
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(color: Colors.red.shade200),
                  ),
                  child: Row(
                    children: [
                      Icon(Icons.error, color: Colors.red.shade700, size: 20),
                      const SizedBox(width: 8),
                      Expanded(
                        child: Text(
                          _error!,
                          style: TextStyle(color: Colors.red.shade700),
                        ),
                      ),
                    ],
                  ),
                ),
              const SizedBox(height: 24),

              // ── Connect Button ──────────────────────────────────────────
              SizedBox(
                width: double.infinity,
                height: 48,
                child: ElevatedButton.icon(
                  onPressed: _connecting
                      ? null
                      : () => _connect(_hostController.text),
                  icon: _connecting
                      ? const SizedBox(
                          width: 20,
                          height: 20,
                          child: CircularProgressIndicator(strokeWidth: 2),
                        )
                      : const Icon(Icons.link),
                  label: Text(_connecting ? 'Connecting...' : 'Connect'),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: Theme.of(context).colorScheme.primary,
                    foregroundColor: Theme.of(context).colorScheme.onPrimary,
                    textStyle: const TextStyle(fontSize: 16),
                  ),
                ),
              ),
              const SizedBox(height: 12),

              // ── Cancel / Exit ───────────────────────────────────────────
              TextButton(
                onPressed: _connecting
                    ? null
                    : () => Navigator.of(context).pop(false),
                child: const Text('Exit'),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// A single tile in the connection history list.
class _HistoryTile extends StatelessWidget {
  final String hostname;
  final VoidCallback? onTap;
  final VoidCallback onDelete;

  const _HistoryTile({
    required this.hostname,
    this.onTap,
    required this.onDelete,
  });

  @override
  Widget build(BuildContext context) {
    return Card(
      margin: const EdgeInsets.only(bottom: 4),
      child: ListTile(
        leading: CircleAvatar(
          backgroundColor: Colors.green.shade100,
          child: Icon(Icons.computer, color: Colors.green.shade700, size: 20),
        ),
        title: Text(
          hostname,
          style: const TextStyle(fontWeight: FontWeight.w500),
        ),
        trailing: IconButton(
          icon: Icon(Icons.close, size: 18, color: Colors.grey.shade500),
          onPressed: onDelete,
          tooltip: 'Remove from history',
        ),
        onTap: onTap,
      ),
    );
  }
}
