# Macros

Uma macro é um arquivo de texto com uma linha de comando do aethercli por
linha. Linhas vazias e linhas cujo primeiro caractere é `#` são ignoradas.

Crie o arquivo no mesmo diretório do `config.json` em uso:

```text
# inventário diário
show system disk
show system memory
show ip interface
```

Salve, por exemplo, como `inventario.macro` e execute:

```text
aethercli# run macro inventario.macro
```

Um caminho relativo é resolvido a partir do diretório do arquivo de
configuração; um caminho absoluto também é aceito:

```text
aethercli# run macro /opt/aethercli/inventario.macro
```

Para listar os arquivos `.macro` disponíveis no diretório da configuração:

```text
aethercli# configure terminal
aethercli(config)# list macros
```

Os comandos são executados na ordem do arquivo. A macro é interrompida no
primeiro comando que falhar, inclusive `exit`. A execução e sua saída são
registradas no diretório da configuração como
`<macro>.<usuário>.<data-hora>.out`. Macros podem chamar outras macros, com
limite de oito níveis de recursão.
