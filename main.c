#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE 512
#define INODE_SIZE 64
#define MAX_FILES 10
#define MAX_BLOCKS 100

typedef struct {
  int size;
  int free_blocks;
  int first_free_block;
  char bitmap[MAX_BLOCKS];
} superblock_t;

typedef struct {
  char filename[32];
  int size;
  int first_block;
  int next_block;
} inode_t;

// Encontra um bloco livre
int find_free_block(FILE *disk) {
  superblock_t sb;
  fseek(disk, 0, SEEK_SET);
  fread(&sb, sizeof(superblock_t), 1, disk);
  if (sb.first_free_block != -1) {
    int block_index = sb.first_free_block;
    sb.first_free_block = sb.bitmap[block_index];
    sb.free_blocks--;
    fseek(disk, 0, SEEK_SET);
    fwrite(&sb, sizeof(superblock_t), 1, disk);
    return block_index;
  }
  return -1;
}

// Encontra um inode livre
int find_free_inode(FILE *disk) {
  inode_t inode;
  for (int i = 0; i < MAX_FILES; i++) {
    fseek(disk, sizeof(superblock_t) + i * sizeof(inode_t), SEEK_SET);
    fread(&inode, sizeof(inode_t), 1, disk);
    if (inode.filename[0] == '\0') {
      return i;
    }
  }
  return -1;
}

// Inicializa o sistema de arquivos
void init_filesystem(FILE *disk) {
  superblock_t sb = {BLOCK_SIZE * MAX_BLOCKS, MAX_BLOCKS, -1, {0}};
  fwrite(&sb, sizeof(superblock_t), 1, disk);

  inode_t empty_inode = {"", 0, -1, -1};
  for (int i = 0; i < MAX_FILES; i++) {
    fwrite(&empty_inode, sizeof(inode_t), 1, disk);
  }
}

// Cria um arquivo
int create_file(FILE *disk, char *filename) {
  int inode_index = find_free_inode(disk);
  if (inode_index == -1) {
    return -1;
  }

  inode_t new_inode = {0};
  strcpy(new_inode.filename, filename);

  fseek(disk, sizeof(superblock_t) + inode_index * sizeof(inode_t), SEEK_SET);
  fwrite(&new_inode, sizeof(inode_t), 1, disk);
  return inode_index;
}

// Lê um arquivo
  int read_file(FILE *disk, char *filename, char *buffer) {
    inode_t inode;
    int inode_index = -1;
    for (int i = 0; i < MAX_FILES; i++) {
        fseek(disk, sizeof(superblock_t) + i * sizeof(inode_t), SEEK_SET);
        fread(&inode, sizeof(inode_t), 1, disk);
        if (strcmp(inode.filename, filename) == 0) {
            inode_index = i;
            break;
        }
    }
    if (inode_index == -1) {
        fprintf(stderr, "Erro: Arquivo não encontrado.\n"); // Mensagem de erro mais clara
        return -1; 
    }

    int bytes_read = 0;
    int current_block = inode.first_block;
    while (current_block != -1 && bytes_read < inode.size) { // Lê até o final do arquivo
        fseek(disk, sizeof(superblock_t) + MAX_FILES * sizeof(inode_t) + current_block * BLOCK_SIZE, SEEK_SET);
        // Calcula corretamente a quantidade de bytes a serem lidos
        int bytes_to_read = (inode.size - bytes_read) < BLOCK_SIZE ? (inode.size - bytes_read) : BLOCK_SIZE;
        int read_bytes = fread(buffer + bytes_read, 1, bytes_to_read, disk);
        if (read_bytes == 0 && ferror(disk)) { // Verifica se houve erro na leitura
            perror("Erro ao ler do disco");
            return -1;
        }
        bytes_read += read_bytes;
        current_block = inode.next_block;
    }
    buffer[bytes_read] = '\0'; // Adiciona terminador de string para evitar problemas
    return bytes_read;
}


// Escreve em um arquivo
int write_file(FILE *disk, char *filename, char *data, int size) {
  inode_t inode;
  int inode_index = -1;
  for (int i = 0; i < MAX_FILES; i++) {
    fseek(disk, sizeof(superblock_t) + i * sizeof(inode_t), SEEK_SET);
    fread(&inode, sizeof(inode_t), 1, disk);
    if (strcmp(inode.filename, filename) == 0) {
      inode_index = i;
      break;
    }
  }
  if (inode_index == -1) {
    return -1;
  }

  int bytes_written = 0;
  int current_block = inode.first_block;
  while (bytes_written < size) {
    int block_to_write = current_block != -1 ? current_block : find_free_block(disk);
    if (block_to_write == -1) {
      return -1;
    }

    fseek(disk, sizeof(superblock_t) + MAX_FILES * sizeof(inode_t) + block_to_write * BLOCK_SIZE, SEEK_SET);
    int bytes_to_write = (size - bytes_written) > BLOCK_SIZE ? BLOCK_SIZE : (size - bytes_written);
    bytes_written += fwrite(data + bytes_written, 1, bytes_to_write, disk);

    if (current_block == -1) {
      inode.first_block = block_to_write;
    } else {
      inode.next_block = block_to_write;
      fseek(disk, sizeof(superblock_t) + (inode_index * sizeof(inode_t)) + (current_block * sizeof(int)), SEEK_SET);
      fwrite(&inode.next_block, sizeof(int), 1, disk);
    }
    current_block = block_to_write;

    superblock_t sb;
    fseek(disk, 0, SEEK_SET);
    fread(&sb, sizeof(superblock_t), 1, disk);
    sb.bitmap[block_to_write] = 1;
    sb.free_blocks--;
    fseek(disk, 0, SEEK_SET);
    fwrite(&sb, sizeof(superblock_t), 1, disk);
  }

  inode.size = size;
  fseek(disk, sizeof(superblock_t) + inode_index * sizeof(inode_t), SEEK_SET);
  fwrite(&inode, sizeof(inode_t), 1, disk);

  return bytes_written;
}

// Lista arquivos
void list_files(FILE *disk) {
  inode_t inode;
  for (int i = 0; i < MAX_FILES; i++) {
    fseek(disk, sizeof(superblock_t) + i * sizeof(inode_t), SEEK_SET);
    fread(&inode, sizeof(inode_t), 1, disk);
    if (inode.filename[0] != '\0') {
      printf("%s\n", inode.filename);
    }
  }
}

// Deleta um arquivo
void delete_file() {
    // Implementação da função delete_file
}

int main() {
  FILE *disk = fopen("meu_disco.bin", "w+b");
  if (disk == NULL) {
    perror("Erro ao abrir o disco");
    return 1;
  }

  init_filesystem(disk);

  char command[16], filename[32], data[BLOCK_SIZE];
  int size;

  while (1) {
    printf("\nComandos:\n");
    printf(" create <nome>\n read <nome>\n write <nome> <dados>\n list\n delete <nome>\n exit\n");
    printf("Entre com o comando: ");
    scanf("%s", command);

    if (strcmp(command, "create") == 0) {
      scanf("%s", filename);
      if (create_file(disk, filename) != -1) {
        printf("Arquivo '%s' criado com sucesso.\n", filename);
      } else {
        printf("Erro ao criar arquivo.\n");
      }
    } else if (strcmp(command, "read") == 0) {
      scanf("%s", filename);
      int bytes_read = read_file(disk, filename, data);
      if (bytes_read != -1) {
        printf("Conteúdo de '%s': %s\n", filename, data);
      } else {
        printf("Erro ao ler arquivo.\n");
      }
    } else if (strcmp(command, "write") == 0) {
      scanf("%s %[^\n]", filename, data);
      size = strlen(data);
      if (write_file(disk, filename, data, size) != -1) {
        printf("Arquivo '%s' escrito com sucesso.\n", filename);
      } else {
        printf("Erro ao escrever no arquivo.\n");
      }
    } else if (strcmp(command, "list") == 0) {
      list_files(disk);
    } else if (strcmp(command, "delete") == 0) {
      scanf("%s", filename);
      delete_file(disk, filename);
    } else if (strcmp(command, "exit") == 0) {
      break;
    } else {
      printf("Comando inválido.\n");
    }
  }

  fclose(disk);
  return 0;
}
