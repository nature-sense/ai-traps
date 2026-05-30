import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import 'widgets/adaptive_app_shell.dart';
import 'providers/trap_provider.dart';
import 'providers/ble_provider.dart';
import 'providers/websocket_provider.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MultiProvider(
      providers: [
        ChangeNotifierProvider(create: (_) => TrapProvider()),
        ChangeNotifierProvider(create: (_) => BleProvider()),
        ChangeNotifierProvider(create: (_) => WebSocketProvider()),
      ],
      child: MaterialApp(
        title: 'AI Insect Trap Manager',
        theme: ThemeData(
          colorScheme: ColorScheme.fromSeed(
            seedColor: Colors.green,
            brightness: Brightness.light,
          ),
          useMaterial3: true,
        ),
        darkTheme: ThemeData(
          colorScheme: ColorScheme.fromSeed(
            seedColor: Colors.green,
            brightness: Brightness.dark,
          ),
          useMaterial3: true,
        ),
        themeMode: ThemeMode.system,
        home: const AdaptiveAppShell(),
        debugShowCheckedModeBanner: false,
      ),
    );
  }
}
