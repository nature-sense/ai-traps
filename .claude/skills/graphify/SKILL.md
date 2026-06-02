---
name: graphify
description: Query and navigate your codebase as a knowledge graph. Use this to find relationships between components, explore architecture, and get context without reading many files.
---

# Graphify Knowledge Graph Skill

## Overview
This skill provides tools to query a pre-built knowledge graph of your codebase. Instead of reading many source files, you can ask the graph for relationships, dependencies, and context.

## Commands

### `/graphify .`
Build or update the knowledge graph from the current directory.

### `/graphify query "<question>"`
Search the graph using BFS (breadth-first search) to find relevant nodes.
- Example: `/graphify query "How does authentication work?"`

### `/graphify path "<node1>" "<node2>"`
Find the shortest relationship path between two components.
- Example: `/graphify path "UserService" "DatabasePool"`

### `/graphify explain "<node>"`
Get a plain-language explanation of what a node represents and its connections.

## Workflow
1. Run `/graphify .` to build the initial graph
2. Ask questions using `/graphify query "..."`
3. After code changes, run `/graphify . --update` to refresh

## Output
The graph is stored in `graphify-out/graph.json` and can be queried without reading source files directly.
