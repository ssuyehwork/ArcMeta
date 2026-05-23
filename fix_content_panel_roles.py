import sys

with open('src/ui/ContentPanel.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

# Fix nameItem->setData(1.0, AspectRatioRole) -> 0.0 to match non-loaded state
# Actually 1.0 is a safer default for square if it fails to load.
# ScanDialog uses 1.0 default in UserRole + 2 check.

# Update dataChanged call to use the new roles
# ContentPanel uses 344 and 353 for dataChanged.
# AspectRatioRole and HasThumbnailRole are what we want to notify.

# The current code already does:
# m_model->dataChanged(pIdx, pIdx, {AspectRatioRole, HasThumbnailRole});
# which is correct.

with open('src/ui/ContentPanel.cpp', 'w', encoding='utf-8') as f:
    f.write(content)
