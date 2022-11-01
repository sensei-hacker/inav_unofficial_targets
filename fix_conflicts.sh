while read file; do git checkout upstream/master "$file" && git add "$file" # && git rebase --continue
done <files.txt && git rebase --continue

